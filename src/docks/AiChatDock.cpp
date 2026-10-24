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
#include "MainWindow.h"
#include "ScintillaNext.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

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

QString defaultBaseUrl(const QString &type)
{
    return type == "anthropic" ? "https://api.anthropic.com" : "https://api.openai.com/v1";
}

QString defaultModel(const QString &type)
{
    return type == "anthropic" ? "claude-sonnet-5-5" : "gpt-4o-mini";
}

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
    for (const char *key : {"path", "command", "pattern"}) {
        const QString value = input[key].toString();
        if (!value.isEmpty())
            return value.left(120);
    }
    return QString();
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
    QPushButton *settingsButton = new QPushButton(tr("Settings"), container);
    clearButton = new QPushButton(tr("New Chat"), container);
    toolbar->addWidget(statusLabel, 1);
    toolbar->addWidget(clearButton);
    toolbar->addWidget(settingsButton);
    layout->addLayout(toolbar);

    chatView = new QTextBrowser(container);
    chatView->setOpenExternalLinks(true);
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
    connect(clearButton, &QPushButton::clicked, this, &AiChatDock::clearChat);
    connect(settingsButton, &QPushButton::clicked, this, &AiChatDock::openSettings);
    connect(insertButton, &QPushButton::clicked, this, &AiChatDock::insertLastCodeBlock);
    connect(explainButton, &QPushButton::clicked, this, &AiChatDock::explainSelection);
    connect(agentMode, &QCheckBox::toggled, this, [](bool checked) { QSettings().setValue("AI/AgentMode", checked); });
    connect(autoApprove, &QCheckBox::toggled, this, [](bool checked) { QSettings().setValue("AI/AutoApprove", checked); });

    setBusy(false);
    renderChat();
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

AiChatDock::Provider AiChatDock::loadProvider() const
{
    QSettings settings;
    Provider p;
    p.type = settings.value("AI/Provider", "anthropic").toString();
    p.baseUrl = settings.value("AI/BaseUrl", defaultBaseUrl(p.type)).toString();
    p.apiKey = settings.value("AI/ApiKey").toString();
    p.model = settings.value("AI/Model", defaultModel(p.type)).toString();
    return p;
}

void AiChatDock::openSettings()
{
    Provider p = loadProvider();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("AI Settings"));
    QFormLayout *form = new QFormLayout(&dialog);

    QComboBox *type = new QComboBox(&dialog);
    type->addItem("Anthropic (Claude)", "anthropic");
    type->addItem("OpenAI-compatible (OpenAI, Ollama, LM Studio, OpenRouter...)", "openai");
    type->setCurrentIndex(type->findData(p.type));
    QLineEdit *baseUrl = new QLineEdit(p.baseUrl, &dialog);
    QLineEdit *apiKey = new QLineEdit(p.apiKey, &dialog);
    apiKey->setEchoMode(QLineEdit::Password);
    apiKey->setPlaceholderText(tr("Not needed for local Ollama"));
    QLineEdit *model = new QLineEdit(p.model, &dialog);
    QLabel *hint = new QLabel(tr("Ollama: choose OpenAI-compatible, URL http://localhost:11434/v1, model e.g. qwen2.5-coder. "
                                 "Agent mode needs a model that supports tool calling."), &dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray;");

    connect(type, &QComboBox::currentIndexChanged, &dialog, [=]() {
        const QString t = type->currentData().toString();
        baseUrl->setText(defaultBaseUrl(t));
        model->setText(defaultModel(t));
    });

    form->addRow(tr("Provider:"), type);
    form->addRow(tr("Base URL:"), baseUrl);
    form->addRow(tr("API Key:"), apiKey);
    form->addRow(tr("Model:"), model);
    form->addRow(hint);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    dialog.resize(560, dialog.sizeHint().height());

    if (dialog.exec() == QDialog::Accepted) {
        QSettings settings;
        settings.setValue("AI/Provider", type->currentData().toString());
        settings.setValue("AI/BaseUrl", baseUrl->text().trimmed());
        settings.setValue("AI/ApiKey", apiKey->text().trimmed());
        settings.setValue("AI/Model", model->text().trimmed());
        renderChat();
    }
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
    if (!agentMode->isChecked())
        return CHAT_PROMPT;

    const QString root = tools->workspaceRoot();
    QString currentFile = "(none)";
    ScintillaNext *editor = window->currentEditor();
    if (editor) {
        currentFile = editor->getName();
        if (editor->isFile() && !root.isEmpty())
            currentFile = QDir(root).relativeFilePath(editor->getFilePath());
    }

    QString prompt = QString(AGENT_PROMPT).arg(root.isEmpty() ? "(none)" : root, currentFile);
    if (root.isEmpty())
        prompt += "\n\nNo workspace folder is open, so file tools will fail. Tell the user to open one with File > Open Folder as Workspace.";
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
    const Provider p = loadProvider();
    if (p.apiKey.isEmpty() && !p.baseUrl.contains("localhost") && !p.baseUrl.contains("127.0.0.1")) {
        openSettings();
        if (loadProvider().apiKey.isEmpty()) {
            input->setPlainText(question);
            return;
        }
    }

    QString content = question;
    if (includeContext->isChecked())
        content = buildContext() + question;

    history.append(QJsonObject{{"role", "user"}, {"content", content}, {"display", question}});
    stopRequested = false;
    agentSteps = 0;
    lastError.clear();
    sendRequest();
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
    const Provider p = loadProvider();
    requestProviderType = p.type;
    pendingAnswer.clear();
    pendingToolCalls.clear();
    streamBuffer.clear();

    QJsonObject body;
    body["model"] = p.model;
    body["stream"] = true;
    QNetworkRequest request;
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString base = p.baseUrl;
    while (base.endsWith('/'))
        base.chop(1);

    const QJsonArray toolDefinitions = AiAgentTools::definitions();

    if (p.type == "anthropic") {
        request.setUrl(QUrl(base + "/v1/messages"));
        request.setRawHeader("x-api-key", p.apiKey.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
        body["max_tokens"] = 16000;
        body["system"] = systemPrompt();
        body["messages"] = anthropicMessages();
        if (agentMode->isChecked()) {
            QJsonArray defs;
            for (const auto &t : toolDefinitions) {
                const QJsonObject o = t.toObject();
                defs.append(QJsonObject{{"name", o["name"]}, {"description", o["description"]}, {"input_schema", o["schema"]}});
            }
            body["tools"] = defs;
        }
    }
    else {
        request.setUrl(QUrl(base + "/chat/completions"));
        if (!p.apiKey.isEmpty())
            request.setRawHeader("Authorization", "Bearer " + p.apiKey.toUtf8());
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

    reply = network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
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
    if (type == "content_block_start") {
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

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool cancelled = reply->error() == QNetworkReply::OperationCanceledError;
    if (reply->error() != QNetworkReply::NoError && !cancelled && lastError.isEmpty()) {
        const QJsonObject obj = QJsonDocument::fromJson(streamBuffer + reply->readAll()).object();
        QString message = obj["error"].toObject()["message"].toString();
        if (message.isEmpty())
            message = reply->errorString();
        lastError = status ? QString("HTTP %1: %2").arg(status).arg(message) : message;
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
            const QJsonDocument args = QJsonDocument::fromJson(pending.arguments.isEmpty() ? "{}" : pending.arguments.toUtf8(), &parseError);
            QJsonObject call{
                {"id", pending.id.isEmpty() ? QString("call_%1_%2").arg(agentSteps).arg(generatedId++) : pending.id},
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

void AiChatDock::clearChat()
{
    if (reply || runningTools)
        return;
    history = QJsonArray();
    lastError.clear();
    renderChat();
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
    QString md;
    if (history.isEmpty() && !reply && lastError.isEmpty()) {
        md = tr("### AI Assistant\n\n"
                "**Agent mode** lets the AI explore your project, edit files (you approve each diff) and run commands. "
                "Open your project with *File > Open Folder as Workspace* first.\n\n"
                "Use **Settings** to choose Claude, OpenAI or a local Ollama model.");
    }

    bool assistantHeader = false;
    for (int i = 0; i < history.size(); ++i) {
        const QJsonObject m = history.at(i).toObject();
        const QString role = m["role"].toString();
        if (role == "user") {
            if (i > 0)
                md += "---\n\n";
            const QString text = m["display"].toString().isEmpty() ? m["content"].toString() : m["display"].toString();
            md += "#### " + tr("You") + "\n\n" + text + "\n\n";
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

    if (!reply && !runningTools) {
        const Provider p = loadProvider();
        statusLabel->setText(p.model.isEmpty() ? tr("No model set") : p.model);
    }
}

void AiChatDock::setBusy(bool busy)
{
    sendButton->setEnabled(!busy);
    stopButton->setEnabled(busy);
    insertButton->setEnabled(!busy);
    clearButton->setEnabled(!busy);
    if (busy && !runningTools)
        statusLabel->setText(tr("Generating..."));
}
