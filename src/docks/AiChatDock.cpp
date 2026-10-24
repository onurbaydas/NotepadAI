/*
 * This file is part of NotepadAI, a fork of Notepad Next.
 *
 * NotepadAI is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NotepadAI is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NotepadAI.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "AiChatDock.h"

#include "AiAgentTools.h"
#include "AiClient.h"
#include "MainWindow.h"
#include "ScintillaNext.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

const char *CHAT_PROMPT =
    "You are a coding assistant embedded in the NotepadAI text editor. "
    "Answer concisely. When you propose code, put it in a single fenced code block "
    "so the user can insert it into the editor with one click. "
    "Reply in the same language the user writes in.";

const char *AGENT_PROMPT =
    "You are an AI coding agent embedded in the NotepadAI code editor, working in the user's project.\n"
    "Workspace root: %1\n"
    "File currently open in the editor: %2\n\n"
    "Use the tools to explore the project, read files, make changes and run commands. "
    "Always read a file before editing it. Prefer edit_file with a small, unique old_string over rewriting whole files. "
    "Paths are relative to the workspace root. Every change is shown to the user as a diff for approval; "
    "if a change is rejected, ask what they want instead. Do not ask for permission before using tools, just use them. "
    "When you are done, briefly summarize what you changed. Reply in the same language the user writes in.";

// Keep requests reasonably sized when the whole file is sent as context.
const int MAX_CONTEXT_CHARS = 60000;

// Upper bound on tool round trips for a single user message.
const int MAX_AGENT_STEPS = 30;

// Context management (roughly 4 characters per token): above COMPACT_AT older tool
// outputs are shortened, above DROP_AT the oldest turns are removed.
const int COMPACT_AT_CHARS = 240000;
const int DROP_AT_CHARS = 480000;
const int KEEP_RECENT_TOOL_RESULTS = 3;
const int COMPACTED_TOOL_RESULT_CHARS = 800;

const int MAX_HISTORY_MENU_ITEMS = 30;

QString lastCodeBlock(const QString &markdown)
{
    static const QRegularExpression re("```[^\\n]*\\n(.*?)```", QRegularExpression::DotMatchesEverythingOption);
    QString code;
    auto it = re.globalMatch(markdown);
    while (it.hasNext())
        code = it.next().captured(1);
    return code;
}

// Short description of a tool call for the chat, e.g. the path or command.
QString toolLabel(const QJsonObject &input)
{
    static const char *const keys[] = {"path", "command", "pattern"};
    for (const char *key : keys) {
        const QString value = input[key].toString();
        if (!value.isEmpty())
            return value.left(120);
    }
    return QString();
}

QString formatTokens(int tokens)
{
    return tokens >= 1000 ? QString::number(tokens / 1000.0, 'f', 1) + "k" : QString::number(tokens);
}

int historySize(const QJsonArray &history)
{
    return QJsonDocument(history).toJson(QJsonDocument::Compact).size();
}

}

AiChatDock::AiChatDock(MainWindow *window) :
    QDockWidget(window),
    window(window),
    tools(new AiAgentTools(window)),
    network(new QNetworkAccessManager(this))
{
    setObjectName("AiChatDock");
    setWindowTitle(tr("AI Assistant"));

    QSettings settings;

    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);

    QHBoxLayout *toolbar = new QHBoxLayout();
    statusLabel = new QLabel(container);
    statusLabel->setStyleSheet("color: gray;");
    statusLabel->setMinimumWidth(40);
    QPushButton *settingsButton = new QPushButton(tr("Settings"), container);
    historyButton = new QPushButton(tr("History"), container);
    clearButton = new QPushButton(tr("New Chat"), container);
    toolbar->addWidget(statusLabel, 1);
    toolbar->addWidget(clearButton);
    toolbar->addWidget(historyButton);
    toolbar->addWidget(settingsButton);
    layout->addLayout(toolbar);

    chatView = new QTextBrowser(container);
    chatView->setOpenLinks(false);
    layout->addWidget(chatView, 1);

    input = new QPlainTextEdit(container);
    input->setPlaceholderText(tr("Ask something or describe a change... (Ctrl+Enter to send)"));
    input->setMaximumHeight(110);
    input->installEventFilter(this);
    setMinimumWidth(260);
    layout->addWidget(input);

    QHBoxLayout *options = new QHBoxLayout();
    agentMode = new QCheckBox(tr("Agent mode"), container);
    agentMode->setToolTip(tr("Let the AI read, search and edit files in the workspace and run commands"));
    agentMode->setChecked(settings.value("AI/AgentMode", true).toBool());
    autoApprove = new QCheckBox(tr("Auto-apply edits"), container);
    autoApprove->setToolTip(tr("Apply file edits without showing the diff first. Commands always ask."));
    autoApprove->setChecked(settings.value("AI/AutoApprove", false).toBool());
    includeContext = new QCheckBox(tr("Send selection / file"), container);
    includeContext->setChecked(true);
    options->addWidget(agentMode);
    options->addWidget(autoApprove);
    options->addWidget(includeContext);
    options->addStretch(1);
    layout->addLayout(options);

    QHBoxLayout *actions = new QHBoxLayout();
    QPushButton *explainButton = new QPushButton(tr("Explain"), container);
    explainButton->setToolTip(tr("Explain the selected code"));
    insertButton = new QPushButton(tr("Insert Code"), container);
    insertButton->setToolTip(tr("Insert the last code block of the answer into the editor (replaces the selection)"));
    stopButton = new QPushButton(tr("Stop"), container);
    sendButton = new QPushButton(tr("Send"), container);
    sendButton->setDefault(true);
    actions->addWidget(explainButton);
    actions->addWidget(insertButton);
    actions->addStretch(1);
    actions->addWidget(stopButton);
    actions->addWidget(sendButton);
    layout->addLayout(actions);

    setWidget(container);

    connect(sendButton, &QPushButton::clicked, this, &AiChatDock::sendMessage);
    connect(stopButton, &QPushButton::clicked, this, &AiChatDock::stopRequest);
    connect(clearButton, &QPushButton::clicked, this, &AiChatDock::newChat);
    connect(historyButton, &QPushButton::clicked, this, &AiChatDock::showHistoryMenu);
    connect(settingsButton, &QPushButton::clicked, this, [this]() {
        if (AiClient::showSettings(this))
            renderChat();
    });
    connect(insertButton, &QPushButton::clicked, this, &AiChatDock::insertLastCodeBlock);
    connect(explainButton, &QPushButton::clicked, this, &AiChatDock::explainSelection);
    connect(chatView, &QTextBrowser::anchorClicked, this, &AiChatDock::linkClicked);
    connect(agentMode, &QCheckBox::toggled, this, [](bool checked) { QSettings().setValue("AI/AgentMode", checked); });
    connect(autoApprove, &QCheckBox::toggled, this, [](bool checked) { QSettings().setValue("AI/AutoApprove", checked); });

    // Remember what each file looked like before the agent first touched it in a turn
    tools->beforeChange = [this](const QString &path, bool existed, const QString &content) {
        QMap<QString, FileSnapshot> &snapshots = checkpoints[nextTurn - 1];
        if (!snapshots.contains(path))
            snapshots.insert(path, FileSnapshot(existed, content));
    };

    setBusy(false);
    renderChat();
}

void AiChatDock::focusInput()
{
    show();
    raise();
    input->setFocus();
}

bool AiChatDock::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == input && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) && (keyEvent->modifiers() & Qt::ControlModifier)) {
            sendMessage();
            return true;
        }
    }
    return QDockWidget::eventFilter(obj, event);
}

QString AiChatDock::buildContext() const
{
    ScintillaNext *editor = window->currentEditor();
    if (!editor)
        return QString();

    QString text = QString::fromUtf8(editor->getSelText());
    QString kind = "Selected code";
    if (text.isEmpty()) {
        // In agent mode the model reads files itself
        if (agentMode->isChecked())
            return QString();
        text = QString::fromUtf8(editor->getText(editor->textLength() + 1));
        kind = "Current file";
    }
    if (text.trimmed().isEmpty())
        return QString();
    if (text.size() > MAX_CONTEXT_CHARS)
        text = text.left(MAX_CONTEXT_CHARS) + "\n... (truncated)";

    return QString("%1 (%2):\n```\n%3\n```\n\n").arg(kind, editor->getName(), text);
}

QString AiChatDock::systemPrompt() const
{
    QString prompt;
    if (!agentMode->isChecked()) {
        prompt = CHAT_PROMPT;
    }
    else {
        const QString root = tools->workspaceRoot();
        QString currentFile = "(none)";
        ScintillaNext *editor = window->currentEditor();
        if (editor) {
            currentFile = editor->getName();
            if (editor->isFile() && !root.isEmpty())
                currentFile = QDir(root).relativeFilePath(editor->getFilePath());
        }

        prompt = QString(AGENT_PROMPT).arg(root.isEmpty() ? "(none)" : root, currentFile);
        if (root.isEmpty())
            prompt += "\n\nNo workspace folder is open, so file tools will fail. Tell the user to open one with File > Open Folder as Workspace.";
    }

    if (historyTrimmed)
        prompt += "\n\nThe beginning of this conversation was removed to stay within the context limit.";
    return prompt;
}

void AiChatDock::sendMessage()
{
    const QString question = input->toPlainText().trimmed();
    if (question.isEmpty() || reply || runningTools)
        return;

    input->clear();
    addUserMessage(question);
}

void AiChatDock::explainSelection()
{
    if (reply || runningTools)
        return;
    addUserMessage(tr("Explain this code."));
}

void AiChatDock::addUserMessage(const QString &question)
{
    if (!AiClient::isConfigured(AiClient::load())) {
        if (!AiClient::showSettings(this) || !AiClient::isConfigured(AiClient::load())) {
            input->setPlainText(question);
            return;
        }
    }

    QString content = question;
    if (includeContext->isChecked())
        content = buildContext() + question;

    if (chatId.isEmpty())
        chatId = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");

    history.append(QJsonObject{{"role", "user"}, {"content", content}, {"display", question}, {"turn", nextTurn++}});
    stopRequested = false;
    agentSteps = 0;
    lastError.clear();
    sendRequest();
}

void AiChatDock::compactHistory()
{
    if (historySize(history) < COMPACT_AT_CHARS)
        return;

    // Shorten old tool outputs; the model can call the tool again if it needs them
    QList<int> toolMessages;
    for (int i = 0; i < history.size(); ++i) {
        if (history.at(i).toObject()["role"].toString() == "tool")
            toolMessages.append(i);
    }
    for (int k = 0; k < toolMessages.size() - KEEP_RECENT_TOOL_RESULTS; ++k) {
        QJsonObject m = history.at(toolMessages[k]).toObject();
        const QString content = m["content"].toString();
        if (m["compacted"].toBool() || content.size() <= COMPACTED_TOOL_RESULT_CHARS * 2)
            continue;
        m["content"] = content.left(COMPACTED_TOOL_RESULT_CHARS) + "\n... [older output removed to save context; call the tool again if you need it]";
        m["compacted"] = true;
        history[toolMessages[k]] = m;
    }

    // Still too big: drop whole turns from the start, always keeping the current one
    while (historySize(history) > DROP_AT_CHARS) {
        int nextUser = -1;
        for (int i = 1; i < history.size(); ++i) {
            if (history.at(i).toObject()["role"].toString() == "user") {
                nextUser = i;
                break;
            }
        }
        if (nextUser < 0)
            break;
        for (int i = 0; i < nextUser; ++i)
            history.removeFirst();
        historyTrimmed = true;
    }
}

QJsonArray AiChatDock::anthropicMessages() const
{
    QJsonArray messages;

    auto append = [&messages](const QString &role, const QJsonObject &block) {
        // Anthropic wants alternating roles; merge consecutive blocks of the same role
        if (!messages.isEmpty() && messages.last().toObject()["role"].toString() == role) {
            QJsonObject last = messages.last().toObject();
            QJsonArray content = last["content"].toArray();
            content.append(block);
            last["content"] = content;
            messages[messages.size() - 1] = last;
        }
        else {
            messages.append(QJsonObject{{"role", role}, {"content", QJsonArray{block}}});
        }
    };

    for (const auto &value : history) {
        const QJsonObject m = value.toObject();
        const QString role = m["role"].toString();
        if (role == "user") {
            append("user", QJsonObject{{"type", "text"}, {"text", m["content"].toString()}});
        }
        else if (role == "assistant") {
            if (!m["content"].toString().isEmpty())
                append("assistant", QJsonObject{{"type", "text"}, {"text", m["content"].toString()}});
            for (const auto &call : m["tool_calls"].toArray()) {
                const QJsonObject c = call.toObject();
                append("assistant", QJsonObject{{"type", "tool_use"}, {"id", c["id"]}, {"name", c["name"]}, {"input", c["input"]}});
            }
        }
        else if (role == "tool") {
            append("user", QJsonObject{{"type", "tool_result"}, {"tool_use_id", m["tool_call_id"]},
                                       {"content", m["content"]}, {"is_error", m["is_error"].toBool()}});
        }
    }

    // Prompt caching: everything up to the newest block is cached and reused by the next request
    if (!messages.isEmpty()) {
        QJsonObject last = messages.last().toObject();
        QJsonArray content = last["content"].toArray();
        QJsonObject block = content.last().toObject();
        block["cache_control"] = QJsonObject{{"type", "ephemeral"}};
        content[content.size() - 1] = block;
        last["content"] = content;
        messages[messages.size() - 1] = last;
    }
    return messages;
}

QJsonArray AiChatDock::openAiMessages() const
{
    QJsonArray messages{QJsonObject{{"role", "system"}, {"content", systemPrompt()}}};

    for (const auto &value : history) {
        const QJsonObject m = value.toObject();
        const QString role = m["role"].toString();
        if (role == "user") {
            messages.append(QJsonObject{{"role", "user"}, {"content", m["content"]}});
        }
        else if (role == "assistant") {
            QJsonObject msg{{"role", "assistant"}, {"content", m["content"]}};
            QJsonArray calls;
            for (const auto &call : m["tool_calls"].toArray()) {
                const QJsonObject c = call.toObject();
                calls.append(QJsonObject{
                    {"id", c["id"]},
                    {"type", "function"},
                    {"function", QJsonObject{
                        {"name", c["name"]},
                        {"arguments", QString::fromUtf8(QJsonDocument(c["input"].toObject()).toJson(QJsonDocument::Compact))}
                    }}
                });
            }
            if (!calls.isEmpty())
                msg["tool_calls"] = calls;
            messages.append(msg);
        }
        else if (role == "tool") {
            messages.append(QJsonObject{{"role", "tool"}, {"tool_call_id", m["tool_call_id"]}, {"content", m["content"]}});
        }
    }
    return messages;
}

void AiChatDock::sendRequest()
{
    compactHistory();

    const AiClient::Provider p = AiClient::load();
    pendingAnswer.clear();
    pendingToolCalls.clear();
    streamBuffer.clear();
    usageText.clear();

    QJsonObject body;
    body["model"] = p.model;
    body["stream"] = true;

    const QJsonArray toolDefinitions = AiAgentTools::definitions();

    if (p.type == "anthropic") {
        body["max_tokens"] = 16000;
        body["system"] = QJsonArray{QJsonObject{{"type", "text"}, {"text", systemPrompt()}, {"cache_control", QJsonObject{{"type", "ephemeral"}}}}};
        body["messages"] = anthropicMessages();
        if (agentMode->isChecked()) {
            QJsonArray defs;
            for (const auto &t : toolDefinitions) {
                const QJsonObject o = t.toObject();
                defs.append(QJsonObject{{"name", o["name"]}, {"description", o["description"]}, {"input_schema", o["schema"]}});
            }
            QJsonObject last = defs.last().toObject();
            last["cache_control"] = QJsonObject{{"type", "ephemeral"}};
            defs[defs.size() - 1] = last;
            body["tools"] = defs;
        }
    }
    else {
        body["messages"] = openAiMessages();
        if (agentMode->isChecked()) {
            QJsonArray defs;
            for (const auto &t : toolDefinitions) {
                const QJsonObject o = t.toObject();
                defs.append(QJsonObject{{"type", "function"}, {"function", QJsonObject{
                    {"name", o["name"]}, {"description", o["description"]}, {"parameters", o["schema"]}}}});
            }
            body["tools"] = defs;
        }
    }

    reply = network->post(AiClient::buildRequest(p), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::readyRead, this, &AiChatDock::handleStreamData);
    connect(reply, &QNetworkReply::finished, this, &AiChatDock::requestFinished);

    setBusy(true);
    renderChat();
}

void AiChatDock::handleStreamData()
{
    if (!reply)
        return;

    streamBuffer += reply->readAll();

    // Error responses are plain JSON, not an event stream; leave them for requestFinished()
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400)
        return;

    int newline;
    while ((newline = streamBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = streamBuffer.left(newline).trimmed();
        streamBuffer.remove(0, newline + 1);
        handleStreamLine(line);
    }
    renderChat();
}

void AiChatDock::handleStreamLine(const QByteArray &line)
{
    if (!line.startsWith("data:"))
        return;

    const QByteArray data = line.mid(5).trimmed();
    if (data == "[DONE]")
        return;

    const QJsonObject obj = QJsonDocument::fromJson(data).object();
    const QString type = obj["type"].toString();

    // Anthropic events
    if (type == "message_start") {
        const QJsonObject usage = obj["message"].toObject()["usage"].toObject();
        const int cached = usage["cache_read_input_tokens"].toInt();
        const int total = usage["input_tokens"].toInt() + cached + usage["cache_creation_input_tokens"].toInt();
        usageText = tr("%1 tokens").arg(formatTokens(total));
        if (cached > 0)
            usageText += " " + tr("(%1 cached)").arg(formatTokens(cached));
    }
    else if (type == "content_block_start") {
        const QJsonObject block = obj["content_block"].toObject();
        if (block["type"].toString() == "tool_use")
            pendingToolCalls[obj["index"].toInt()] = PendingToolCall{block["id"].toString(), block["name"].toString(), QString()};
    }
    else if (type == "content_block_delta") {
        const QJsonObject delta = obj["delta"].toObject();
        if (delta["type"].toString() == "input_json_delta")
            pendingToolCalls[obj["index"].toInt()].arguments += delta["partial_json"].toString();
        else
            pendingAnswer += delta["text"].toString();
    }
    else if (type == "error") {
        lastError = obj["error"].toObject()["message"].toString();
    }
    // OpenAI-compatible chunks
    else if (obj.contains("choices")) {
        const QJsonArray choices = obj["choices"].toArray();
        if (choices.isEmpty())
            return;
        const QJsonObject delta = choices.first().toObject()["delta"].toObject();
        pendingAnswer += delta["content"].toString();

        const QJsonArray calls = delta["tool_calls"].toArray();
        for (int i = 0; i < calls.size(); ++i) {
            const QJsonObject call = calls[i].toObject();
            PendingToolCall &pending = pendingToolCalls[call["index"].toInt(i)];
            if (!call["id"].toString().isEmpty())
                pending.id = call["id"].toString();
            const QJsonObject function = call["function"].toObject();
            if (!function["name"].toString().isEmpty())
                pending.name = function["name"].toString();
            pending.arguments += function["arguments"].toString();
        }
    }
    else if (obj.contains("error")) {
        lastError = obj["error"].toObject()["message"].toString();
    }
}

void AiChatDock::requestFinished()
{
    if (!reply)
        return;

    const bool cancelled = reply->error() == QNetworkReply::OperationCanceledError;
    if (reply->error() != QNetworkReply::NoError && !cancelled && lastError.isEmpty()) {
        lastError = AiClient::errorMessage(reply, streamBuffer + reply->readAll());
    }
    else if (!streamBuffer.trimmed().isEmpty()) {
        handleStreamLine(streamBuffer.trimmed());
    }

    reply->deleteLater();
    reply = nullptr;

    // Tool calls from an interrupted or failed response are incomplete; drop them
    QJsonArray calls;
    if (!cancelled && lastError.isEmpty()) {
        int generatedId = 0;
        for (const PendingToolCall &pending : std::as_const(pendingToolCalls)) {
            if (pending.name.isEmpty())
                continue;
            QJsonParseError parseError;
            const QJsonDocument args = QJsonDocument::fromJson(pending.arguments.isEmpty() ? QByteArray("{}") : pending.arguments.toUtf8(), &parseError);
            QJsonObject call{
                {"id", pending.id.isEmpty() ? QString("call_%1_%2_%3").arg(nextTurn).arg(agentSteps).arg(generatedId++) : pending.id},
                {"name", pending.name},
                {"input", args.object()}
            };
            if (parseError.error != QJsonParseError::NoError)
                call["invalid_arguments"] = true;
            calls.append(call);
        }
    }

    if (!pendingAnswer.isEmpty() || !calls.isEmpty()) {
        QJsonObject message{{"role", "assistant"}, {"content", pendingAnswer}};
        if (!calls.isEmpty())
            message["tool_calls"] = calls;
        history.append(message);
    }
    else if (!history.isEmpty() && history.last().toObject()["role"].toString() == "user") {
        // Nothing came back: drop the question so the conversation stays valid, and give it back to the user
        const QString question = history.last().toObject()["display"].toString();
        history.removeLast();
        if (input->toPlainText().isEmpty())
            input->setPlainText(question);
        if (lastError.isEmpty())
            lastError = tr("Request stopped.");
    }

    pendingAnswer.clear();
    pendingToolCalls.clear();
    saveChat();

    if (!calls.isEmpty() && !stopRequested) {
        QTimer::singleShot(0, this, &AiChatDock::runToolCalls);
    }
    else {
        setBusy(false);
    }
    renderChat();
}

void AiChatDock::runToolCalls()
{
    runningTools = true;
    setBusy(true);

    const QJsonArray calls = history.last().toObject()["tool_calls"].toArray();
    for (const auto &value : calls) {
        const QJsonObject call = value.toObject();
        const QString name = call["name"].toString();
        const QJsonObject callInput = call["input"].toObject();

        AiAgentTools::Result result;
        if (stopRequested) {
            result = {"Cancelled by the user.", tr("cancelled"), true};
        }
        else if (call["invalid_arguments"].toBool()) {
            result = {"The tool arguments were not valid JSON (the response may have been cut off). Try again with smaller steps.", tr("invalid arguments"), true};
        }
        else {
            statusLabel->setText(tr("Running %1...").arg(name));
            result = tools->execute(name, callInput, autoApprove->isChecked());
        }

        history.append(QJsonObject{
            {"role", "tool"},
            {"tool_call_id", call["id"]},
            {"name", name},
            {"label", toolLabel(callInput)},
            {"content", result.content},
            {"summary", result.summary},
            {"is_error", result.isError}
        });
        renderChat();
    }

    runningTools = false;
    saveChat();

    if (stopRequested) {
        setBusy(false);
        renderChat();
        return;
    }
    if (++agentSteps >= MAX_AGENT_STEPS) {
        lastError = tr("Stopped after %1 steps. Send a message to continue.").arg(MAX_AGENT_STEPS);
        setBusy(false);
        renderChat();
        return;
    }

    sendRequest();
}

void AiChatDock::stopRequest()
{
    stopRequested = true;
    if (reply)
        reply->abort();
}

void AiChatDock::newChat()
{
    if (reply || runningTools)
        return;
    history = QJsonArray();
    chatId.clear();
    checkpoints.clear();
    nextTurn = 1;
    historyTrimmed = false;
    lastError.clear();
    usageText.clear();
    renderChat();
}

void AiChatDock::linkClicked(const QUrl &url)
{
    if (url.scheme() == "restore")
        restoreCheckpoint(url.path().toInt());
    else
        QDesktopServices::openUrl(url);
}

void AiChatDock::restoreCheckpoint(int turn)
{
    if (reply || runningTools)
        return;

    // Undo this turn and every later one, newest first, so each file ends at its oldest snapshot
    QList<int> turns;
    QSet<QString> files;
    for (auto it = checkpoints.cbegin(); it != checkpoints.cend(); ++it) {
        if (it.key() >= turn) {
            turns.prepend(it.key());
            for (const QString &path : it.value().keys())
                files.insert(path);
        }
    }

    const auto answer = QMessageBox::question(this, tr("Restore Checkpoint"),
        tr("Revert %n file(s) changed by the AI since this message and remove the conversation from this message on?\n\n"
           "Effects of commands the AI ran (for example installed packages) are not undone.", "", files.size()));
    if (answer != QMessageBox::Yes)
        return;

    QStringList failed;
    for (int t : std::as_const(turns)) {
        const QMap<QString, FileSnapshot> &snapshots = checkpoints[t];
        for (auto it = snapshots.cbegin(); it != snapshots.cend(); ++it) {
            if (!tools->restoreFile(it.key(), it.value().first, it.value().second))
                failed.append(it.key());
        }
        checkpoints.remove(t);
    }

    for (int i = 0; i < history.size(); ++i) {
        const QJsonObject m = history.at(i).toObject();
        if (m["role"].toString() == "user" && m["turn"].toInt() == turn) {
            input->setPlainText(m["display"].toString());
            while (history.size() > i)
                history.removeLast();
            break;
        }
    }

    lastError = failed.isEmpty() ? QString() : tr("Could not restore: %1").arg(failed.join(", "));
    saveChat();
    renderChat();
}

QString AiChatDock::chatsDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/chats";
}

void AiChatDock::saveChat()
{
    if (chatId.isEmpty())
        return;

    const QString path = chatsDirectory() + "/" + chatId + ".json";
    if (history.isEmpty()) {
        QFile::remove(path);
        return;
    }

    QString title;
    for (const auto &value : history) {
        const QJsonObject m = value.toObject();
        if (m["role"].toString() == "user") {
            title = m["display"].toString().simplified().left(80);
            break;
        }
    }

    const QJsonObject chat{
        {"id", chatId},
        {"title", title},
        {"updated", QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"workspace", tools->workspaceRoot()},
        {"trimmed", historyTrimmed},
        {"history", history}
    };

    QDir().mkpath(chatsDirectory());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(chat).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void AiChatDock::loadChat(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject chat = QJsonDocument::fromJson(file.readAll()).object();

    newChat();
    chatId = chat["id"].toString();
    history = chat["history"].toArray();
    historyTrimmed = chat["trimmed"].toBool();
    for (const auto &value : std::as_const(history))
        nextTurn = qMax(nextTurn, value.toObject()["turn"].toInt() + 1);
    renderChat();
    chatView->verticalScrollBar()->setValue(chatView->verticalScrollBar()->maximum());
}

void AiChatDock::showHistoryMenu()
{
    if (reply || runningTools)
        return;

    QFileInfoList files = QDir(chatsDirectory()).entryInfoList({"*.json"}, QDir::Files, QDir::Time);
    QMenu menu(this);
    if (files.isEmpty())
        menu.addAction(tr("No saved chats"))->setEnabled(false);

    for (const QFileInfo &info : files.mid(0, MAX_HISTORY_MENU_ITEMS)) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject chat = QJsonDocument::fromJson(file.readAll()).object();
        const QString title = chat["title"].toString().isEmpty() ? info.baseName() : chat["title"].toString();
        const QString when = info.lastModified().toString("dd.MM HH:mm");
        QAction *action = menu.addAction(QString("%1  —  %2").arg(when, title));
        const QString path = info.filePath();
        connect(action, &QAction::triggered, this, [this, path]() { loadChat(path); });
    }

    if (!chatId.isEmpty()) {
        menu.addSeparator();
        connect(menu.addAction(tr("Delete Current Chat")), &QAction::triggered, this, [this]() {
            QFile::remove(chatsDirectory() + "/" + chatId + ".json");
            newChat();
        });
    }

    menu.exec(historyButton->mapToGlobal(QPoint(0, historyButton->height())));
}

void AiChatDock::insertLastCodeBlock()
{
    ScintillaNext *editor = window->currentEditor();
    if (!editor)
        return;

    for (int i = history.size() - 1; i >= 0; --i) {
        const QJsonObject m = history.at(i).toObject();
        if (m["role"].toString() != "assistant" || m["content"].toString().isEmpty())
            continue;
        const QString code = lastCodeBlock(m["content"].toString());
        if (code.isEmpty())
            break;
        editor->replaceSel(code.toUtf8().constData());
        editor->QWidget::setFocus();
        return;
    }
    statusLabel->setText(tr("No code block in the last answer."));
}

void AiChatDock::renderChat()
{
    const bool busy = reply || runningTools;
    QString md;
    if (history.isEmpty() && !reply && lastError.isEmpty()) {
        md = tr("### AI Assistant\n\n"
                "**Agent mode** lets the AI explore your project, edit files (you approve each diff) and run commands. "
                "Open your project with *File > Open Folder as Workspace* first.\n\n"
                "In the editor: **Ctrl+I** edits the selection with AI, suggestions while typing are accepted with **Tab**, "
                "**Ctrl+L** jumps here.\n\n"
                "Use **Settings** to sign in with OpenRouter in your browser, or choose Claude, OpenAI or a local Ollama model.");
    }
    if (historyTrimmed)
        md += "_" + tr("Older messages were removed to stay within the context limit.") + "_\n\n";

    bool assistantHeader = false;
    for (int i = 0; i < history.size(); ++i) {
        const QJsonObject m = history.at(i).toObject();
        const QString role = m["role"].toString();
        if (role == "user") {
            if (i > 0)
                md += "---\n\n";
            const QString text = m["display"].toString().isEmpty() ? m["content"].toString() : m["display"].toString();
            md += "#### " + tr("You") + "\n\n" + text + "\n\n";

            const int turn = m["turn"].toInt();
            if (!busy && checkpoints.contains(turn) && !checkpoints[turn].isEmpty()) {
                md += QString("[↶ %1](restore:%2) — %3\n\n")
                          .arg(tr("Restore checkpoint"))
                          .arg(turn)
                          .arg(tr("%n file(s) changed", "", checkpoints[turn].size()));
            }
            assistantHeader = false;
        }
        else if (role == "assistant") {
            if (!assistantHeader) {
                md += "#### " + tr("AI") + "\n\n";
                assistantHeader = true;
            }
            if (!m["content"].toString().isEmpty())
                md += m["content"].toString() + "\n\n";
        }
        else if (role == "tool") {
            const QString label = m["label"].toString();
            md += QString("> %1 **%2**%3 — %4%5\n\n")
                      .arg(m["is_error"].toBool() ? "✗" : "✓",
                           m["name"].toString(),
                           label.isEmpty() ? QString() : " `" + label + "`",
                           m["is_error"].toBool() ? "*" : "",
                           m["summary"].toString() + (m["is_error"].toBool() ? "*" : ""));
        }
    }

    if (reply) {
        if (!assistantHeader)
            md += "#### " + tr("AI") + "\n\n";
        if (!pendingAnswer.isEmpty())
            md += pendingAnswer + "\n\n";
        for (const PendingToolCall &pending : std::as_const(pendingToolCalls))
            md += QString("> … **%1**\n\n").arg(pending.name);
        if (pendingAnswer.isEmpty() && pendingToolCalls.isEmpty())
            md += "_" + tr("thinking...") + "_\n\n";
    }
    if (!lastError.isEmpty())
        md += "**" + tr("Error") + ":** " + lastError + "\n";

    QScrollBar *bar = chatView->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    chatView->setMarkdown(md);
    if (atBottom)
        bar->setValue(bar->maximum());

    if (!busy) {
        const AiClient::Provider p = AiClient::load();
        QString status = p.model.isEmpty() ? tr("No model set") : p.model;
        if (!usageText.isEmpty())
            status += " · " + usageText;
        statusLabel->setText(status);
        statusLabel->setToolTip(status);
    }
}

void AiChatDock::setBusy(bool busy)
{
    sendButton->setEnabled(!busy);
    stopButton->setEnabled(busy);
    insertButton->setEnabled(!busy);
    clearButton->setEnabled(!busy);
    historyButton->setEnabled(!busy);
    if (busy && !runningTools)
        statusLabel->setText(tr("Generating..."));
}
