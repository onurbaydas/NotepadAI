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

#include "MainWindow.h"
#include "ScintillaNext.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <QVBoxLayout>

namespace {

const char *SYSTEM_PROMPT =
    "You are a coding assistant embedded in the NotepadAI text editor. "
    "Answer concisely. When you propose code, put it in a single fenced code block "
    "so the user can insert it into the editor with one click. "
    "Reply in the same language the user writes in.";

// Keep requests reasonably sized when the whole file is sent as context.
const int MAX_CONTEXT_CHARS = 60000;

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

}

AiChatDock::AiChatDock(MainWindow *window) :
    QDockWidget(window),
    window(window),
    network(new QNetworkAccessManager(this))
{
    setObjectName("AiChatDock");
    setWindowTitle(tr("AI Assistant"));

    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);

    QHBoxLayout *toolbar = new QHBoxLayout();
    statusLabel = new QLabel(container);
    statusLabel->setStyleSheet("color: gray;");
    QPushButton *settingsButton = new QPushButton(tr("Settings"), container);
    QPushButton *clearButton = new QPushButton(tr("New Chat"), container);
    toolbar->addWidget(statusLabel, 1);
    toolbar->addWidget(clearButton);
    toolbar->addWidget(settingsButton);
    layout->addLayout(toolbar);

    chatView = new QTextBrowser(container);
    chatView->setOpenExternalLinks(true);
    layout->addWidget(chatView, 1);

    input = new QPlainTextEdit(container);
    input->setPlaceholderText(tr("Ask something... (Ctrl+Enter to send)"));
    input->setMaximumHeight(110);
    setMinimumWidth(260);
    input->installEventFilter(this);
    layout->addWidget(input);

    QHBoxLayout *actions = new QHBoxLayout();
    includeContext = new QCheckBox(tr("Send selection / current file"), container);
    includeContext->setChecked(true);
    QPushButton *explainButton = new QPushButton(tr("Explain"), container);
    explainButton->setToolTip(tr("Explain the selected code"));
    insertButton = new QPushButton(tr("Insert Code"), container);
    insertButton->setToolTip(tr("Insert the last code block of the answer into the editor (replaces the selection)"));
    stopButton = new QPushButton(tr("Stop"), container);
    sendButton = new QPushButton(tr("Send"), container);
    sendButton->setDefault(true);
    layout->addWidget(includeContext);
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
    QLabel *hint = new QLabel(tr("Ollama: choose OpenAI-compatible, URL http://localhost:11434/v1, model e.g. qwen2.5-coder"), &dialog);
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
        text = QString::fromUtf8(editor->getText(editor->textLength() + 1));
        kind = "Current file";
    }
    if (text.trimmed().isEmpty())
        return QString();
    if (text.size() > MAX_CONTEXT_CHARS)
        text = text.left(MAX_CONTEXT_CHARS) + "\n... (truncated)";

    return QString("%1 (%2):\n```\n%3\n```\n\n").arg(kind, editor->getName(), text);
}

void AiChatDock::sendMessage()
{
    const QString question = input->toPlainText().trimmed();
    if (question.isEmpty() || reply)
        return;

    input->clear();
    startRequest(question);
}

void AiChatDock::explainSelection()
{
    if (reply)
        return;
    startRequest(tr("Explain this code."));
}

void AiChatDock::startRequest(const QString &question)
{
    const Provider p = loadProvider();
    if (p.apiKey.isEmpty() && !p.baseUrl.contains("localhost") && !p.baseUrl.contains("127.0.0.1")) {
        openSettings();
        if (loadProvider().apiKey.isEmpty())
            return;
        return startRequest(question);
    }

    QString content = question;
    if (includeContext->isChecked())
        content = buildContext() + question;

    history.append(QJsonObject{{"role", "user"}, {"content", content}});
    pendingAnswer.clear();
    lastError.clear();
    streamBuffer.clear();

    QJsonObject body;
    body["model"] = p.model;
    body["stream"] = true;
    QNetworkRequest request;
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString base = p.baseUrl;
    while (base.endsWith('/'))
        base.chop(1);

    if (p.type == "anthropic") {
        request.setUrl(QUrl(base + "/v1/messages"));
        request.setRawHeader("x-api-key", p.apiKey.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
        body["max_tokens"] = 8192;
        body["system"] = SYSTEM_PROMPT;
        body["messages"] = history;
    }
    else {
        request.setUrl(QUrl(base + "/chat/completions"));
        if (!p.apiKey.isEmpty())
            request.setRawHeader("Authorization", "Bearer " + p.apiKey.toUtf8());
        QJsonArray messages{QJsonObject{{"role", "system"}, {"content", SYSTEM_PROMPT}}};
        for (const auto &m : history)
            messages.append(m);
        body["messages"] = messages;
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

    // Anthropic: {"type":"content_block_delta","delta":{"type":"text_delta","text":"..."}}
    if (obj["type"].toString() == "content_block_delta") {
        pendingAnswer += obj["delta"].toObject()["text"].toString();
    }
    else if (obj["type"].toString() == "error") {
        lastError = obj["error"].toObject()["message"].toString();
    }
    // OpenAI-compatible: {"choices":[{"delta":{"content":"..."}}]}
    else if (obj.contains("choices")) {
        const QJsonArray choices = obj["choices"].toArray();
        if (!choices.isEmpty())
            pendingAnswer += choices.first().toObject()["delta"].toObject()["content"].toString();
    }
}

void AiChatDock::requestFinished()
{
    if (!reply)
        return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && reply->error() != QNetworkReply::OperationCanceledError && lastError.isEmpty()) {
        const QJsonObject obj = QJsonDocument::fromJson(streamBuffer + reply->readAll()).object();
        QString message = obj["error"].toObject()["message"].toString();
        if (message.isEmpty())
            message = reply->errorString();
        lastError = status ? QString("HTTP %1: %2").arg(status).arg(message) : message;
    }
    else if (!streamBuffer.trimmed().isEmpty()) {
        handleStreamLine(streamBuffer.trimmed());
    }

    if (!pendingAnswer.isEmpty()) {
        history.append(QJsonObject{{"role", "assistant"}, {"content", pendingAnswer}});
    }
    else {
        // Drop the unanswered question so the conversation stays valid for the next request
        history.removeLast();
        if (lastError.isEmpty())
            lastError = tr("Request stopped.");
    }

    pendingAnswer.clear();
    reply->deleteLater();
    reply = nullptr;
    setBusy(false);
    renderChat();
}

void AiChatDock::stopRequest()
{
    if (reply)
        reply->abort();
}

void AiChatDock::clearChat()
{
    stopRequest();
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
        if (m["role"].toString() != "assistant")
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
    if (history.isEmpty() && pendingAnswer.isEmpty() && lastError.isEmpty()) {
        md = tr("### AI Assistant\n\nSelect code or open a file, then ask a question. "
                "Use **Settings** to choose Claude, OpenAI or a local Ollama model.");
    }

    for (const auto &value : history) {
        const QJsonObject m = value.toObject();
        if (m["role"].toString() == "user")
            md += "#### " + tr("You") + "\n\n" + m["content"].toString() + "\n\n---\n\n";
        else
            md += "#### " + tr("AI") + "\n\n" + m["content"].toString() + "\n\n---\n\n";
    }
    if (reply) {
        md += "#### " + tr("AI") + "\n\n" + (pendingAnswer.isEmpty() ? QString("_...") + tr("thinking") + "_" : pendingAnswer) + "\n\n";
    }
    if (!lastError.isEmpty())
        md += "**" + tr("Error") + ":** " + lastError + "\n";

    QScrollBar *bar = chatView->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    chatView->setMarkdown(md);
    if (atBottom)
        bar->setValue(bar->maximum());

    const Provider p = loadProvider();
    if (!reply)
        statusLabel->setText(p.model.isEmpty() ? tr("No model set") : p.model);
}

void AiChatDock::setBusy(bool busy)
{
    sendButton->setEnabled(!busy);
    stopButton->setEnabled(busy);
    insertButton->setEnabled(!busy);
    if (busy)
        statusLabel->setText(tr("Generating..."));
}
