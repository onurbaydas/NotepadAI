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

#include "AiClient.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace {

const char *OPENROUTER_URL = "https://openrouter.ai/api/v1";
const char *OPENROUTER_DEFAULT_MODEL = "anthropic/claude-sonnet-4.5";

// OpenRouter accepts localhost callbacks on port 3000
const quint16 LOGIN_CALLBACK_PORT = 3000;
const int LOGIN_TIMEOUT_MS = 5 * 60 * 1000;

QString defaultBaseUrl(const QString &type)
{
    return type == "anthropic" ? "https://api.anthropic.com" : "https://api.openai.com/v1";
}

QString defaultModel(const QString &type)
{
    return type == "anthropic" ? "claude-sonnet-5-5" : "gpt-4o-mini";
}

QString defaultCompletionModel(const QString &type)
{
    return type == "anthropic" ? "claude-haiku-5-5" : QString();
}

QByteArray base64Url(const QByteArray &data)
{
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

}

AiClient::AiClient(QObject *parent) :
    QObject(parent),
    manager(new QNetworkAccessManager(this))
{
}

AiClient::Provider AiClient::load()
{
    QSettings settings;
    Provider p;
    p.type = settings.value("AI/Provider", "anthropic").toString();
    p.baseUrl = settings.value("AI/BaseUrl", defaultBaseUrl(p.type)).toString();
    p.apiKey = settings.value("AI/ApiKey").toString();
    p.model = settings.value("AI/Model", defaultModel(p.type)).toString();
    p.completionModel = settings.value("AI/CompletionModel", defaultCompletionModel(p.type)).toString();
    return p;
}

bool AiClient::isConfigured(const Provider &p)
{
    if (p.model.isEmpty() || p.baseUrl.isEmpty())
        return false;
    // Local servers (Ollama, LM Studio) don't need a key
    return !p.apiKey.isEmpty() || p.baseUrl.contains("localhost") || p.baseUrl.contains("127.0.0.1");
}

QNetworkRequest AiClient::buildRequest(const Provider &p)
{
    QString base = p.baseUrl;
    while (base.endsWith('/'))
        base.chop(1);

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    if (p.type == "anthropic") {
        request.setUrl(QUrl(base + "/v1/messages"));
        request.setRawHeader("x-api-key", p.apiKey.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
    }
    else {
        request.setUrl(QUrl(base + "/chat/completions"));
        if (!p.apiKey.isEmpty())
            request.setRawHeader("Authorization", "Bearer " + p.apiKey.toUtf8());
        if (base.contains("openrouter.ai")) {
            request.setRawHeader("HTTP-Referer", "https://github.com/onurbaydas/NotepadAI");
            request.setRawHeader("X-Title", "NotepadAI");
        }
    }
    return request;
}

QString AiClient::errorMessage(QNetworkReply *reply, const QByteArray &body)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    QString message = obj["error"].toObject()["message"].toString();
    if (message.isEmpty())
        message = obj["error"].toString();
    if (message.isEmpty())
        message = reply->errorString();
    return status ? QString("HTTP %1: %2").arg(status).arg(message) : message;
}

QNetworkReply *AiClient::complete(const QString &system, const QString &user, int maxTokens, bool useCompletionModel, const Callback &done)
{
    const Provider p = load();
    const QString model = useCompletionModel && !p.completionModel.isEmpty() ? p.completionModel : p.model;

    QJsonObject body;
    body["model"] = model;
    body["max_tokens"] = maxTokens;
    if (p.type == "anthropic") {
        body["system"] = system;
        body["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", user}}};
    }
    else {
        body["messages"] = QJsonArray{
            QJsonObject{{"role", "system"}, {"content", system}},
            QJsonObject{{"role", "user"}, {"content", user}}
        };
    }

    QNetworkReply *reply = manager->post(buildRequest(p), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, done, type = p.type]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;

        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            done(QString(), errorMessage(reply, data));
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(data).object();
        QString text;
        if (type == "anthropic") {
            for (const auto &block : obj["content"].toArray())
                text += block.toObject()["text"].toString();
        }
        else {
            const QJsonArray choices = obj["choices"].toArray();
            if (!choices.isEmpty())
                text = choices.first().toObject()["message"].toObject()["content"].toString();
        }
        done(text, QString());
    });
    return reply;
}

bool AiClient::showSettings(QWidget *parent)
{
    const Provider p = load();
    QSettings settings;

    QDialog dialog(parent);
    dialog.setWindowTitle(tr("AI Settings"));
    QFormLayout *form = new QFormLayout(&dialog);

    QPushButton *signIn = new QPushButton(tr("Sign in with OpenRouter (browser)"), &dialog);
    signIn->setToolTip(tr("Log in in your browser; no API key copy-paste needed. Gives access to Claude, GPT, Gemini and more."));
    QLabel *signInStatus = new QLabel(&dialog);
    signInStatus->setWordWrap(true);

    QComboBox *type = new QComboBox(&dialog);
    type->addItem("Anthropic (Claude)", "anthropic");
    type->addItem("OpenAI-compatible (OpenAI, OpenRouter, Ollama, LM Studio...)", "openai");
    type->setCurrentIndex(type->findData(p.type));
    QLineEdit *baseUrl = new QLineEdit(p.baseUrl, &dialog);
    QLineEdit *apiKey = new QLineEdit(p.apiKey, &dialog);
    apiKey->setEchoMode(QLineEdit::Password);
    apiKey->setPlaceholderText(tr("Not needed for local Ollama"));
    QLineEdit *model = new QLineEdit(p.model, &dialog);
    QLineEdit *completionModel = new QLineEdit(p.completionModel, &dialog);
    completionModel->setPlaceholderText(tr("Same as chat model"));
    QCheckBox *autocomplete = new QCheckBox(tr("Suggest code while typing (accept with Tab)"), &dialog);
    autocomplete->setChecked(settings.value("AI/Autocomplete", true).toBool());
    QCheckBox *autoApprove = new QCheckBox(tr("Apply the agent's file changes without asking (commands always ask)"), &dialog);
    autoApprove->setChecked(settings.value("AI/AutoApprove", false).toBool());

    QLabel *hint = new QLabel(tr("Ollama: choose OpenAI-compatible, URL http://localhost:11434/v1, model e.g. qwen2.5-coder. "
                                 "Agent mode needs a model that supports tool calling."), &dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray;");

    connect(type, &QComboBox::currentIndexChanged, &dialog, [=]() {
        const QString t = type->currentData().toString();
        baseUrl->setText(defaultBaseUrl(t));
        model->setText(defaultModel(t));
        completionModel->setText(defaultCompletionModel(t));
    });

    connect(signIn, &QPushButton::clicked, &dialog, [=, &dialog]() {
        OpenRouterLogin *login = new OpenRouterLogin(&dialog);
        signIn->setEnabled(false);
        signInStatus->setText(tr("Waiting for the browser login..."));
        connect(login, &OpenRouterLogin::finished, &dialog, [=](const QString &key, const QString &error) {
            login->deleteLater();
            signIn->setEnabled(true);
            if (!error.isEmpty()) {
                signInStatus->setText(tr("Sign in failed: %1").arg(error));
                return;
            }
            type->setCurrentIndex(type->findData("openai"));
            baseUrl->setText(OPENROUTER_URL);
            apiKey->setText(key);
            model->setText(OPENROUTER_DEFAULT_MODEL);
            completionModel->clear();
            signInStatus->setText(tr("Signed in. Press OK to save. You can pick any model listed on openrouter.ai/models."));
        });
        login->start();
    });

    form->addRow(signIn);
    form->addRow(signInStatus);
    form->addRow(tr("Provider:"), type);
    form->addRow(tr("Base URL:"), baseUrl);
    form->addRow(tr("API Key:"), apiKey);
    form->addRow(tr("Model:"), model);
    form->addRow(tr("Autocomplete model:"), completionModel);
    form->addRow(autocomplete);
    form->addRow(autoApprove);
    form->addRow(hint);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    dialog.resize(600, dialog.sizeHint().height());

    if (dialog.exec() != QDialog::Accepted)
        return false;

    settings.setValue("AI/Provider", type->currentData().toString());
    settings.setValue("AI/BaseUrl", baseUrl->text().trimmed());
    settings.setValue("AI/ApiKey", apiKey->text().trimmed());
    settings.setValue("AI/Model", model->text().trimmed());
    settings.setValue("AI/CompletionModel", completionModel->text().trimmed());
    settings.setValue("AI/Autocomplete", autocomplete->isChecked());
    settings.setValue("AI/AutoApprove", autoApprove->isChecked());
    return true;
}

OpenRouterLogin::OpenRouterLogin(QObject *parent) :
    QObject(parent),
    manager(new QNetworkAccessManager(this))
{
}

void OpenRouterLogin::start()
{
    QByteArray random(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(random.data()), random.size() / 4);
    verifier = base64Url(random);
    const QByteArray challenge = base64Url(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256));

    QTcpServer *server = new QTcpServer(this);
    if (!server->listen(QHostAddress::LocalHost, LOGIN_CALLBACK_PORT)) {
        emit finished(QString(), tr("Port %1 is in use. Close the program using it and try again.").arg(LOGIN_CALLBACK_PORT));
        return;
    }

    QTimer::singleShot(LOGIN_TIMEOUT_MS, server, [this, server]() {
        server->close();
        emit finished(QString(), tr("Timed out waiting for the browser."));
    });

    connect(server, &QTcpServer::newConnection, this, [this, server]() {
        QTcpSocket *socket = server->nextPendingConnection();
        // Keep the socket alive until the response is flushed, even after the server is gone
        socket->setParent(this);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, server, socket]() {
            const QByteArray request = socket->peek(8192);
            if (!request.contains("\r\n\r\n"))
                return;

            // "GET /callback?code=... HTTP/1.1"
            const QByteArray target = request.split(' ').value(1);
            const QString code = QUrlQuery(QUrl(QString::fromUtf8(target)).query()).queryItemValue("code");

            const QByteArray html = code.isEmpty()
                ? "<html><body style='font-family:sans-serif'><h2>Sign in failed.</h2></body></html>"
                : "<html><body style='font-family:sans-serif'><h2>NotepadAI: signed in.</h2><p>You can close this tab and return to the app.</p></body></html>";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                          + QByteArray::number(html.size()) + "\r\n\r\n" + html);
            socket->disconnectFromHost();

            if (code.isEmpty())
                return;  // e.g. favicon request; keep waiting for the real callback

            server->close();
            server->deleteLater();
            exchangeCode(code);
        });
    });

    QUrl url("https://openrouter.ai/auth");
    QUrlQuery query;
    query.addQueryItem("callback_url", QString("http://localhost:%1/callback").arg(LOGIN_CALLBACK_PORT));
    query.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    query.addQueryItem("code_challenge_method", "S256");
    url.setQuery(query);
    QDesktopServices::openUrl(url);
}

void OpenRouterLogin::exchangeCode(const QString &code)
{
    QNetworkRequest request(QUrl(QString(OPENROUTER_URL) + "/auth/keys"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QJsonObject body{{"code", code}, {"code_verifier", QString::fromLatin1(verifier)}, {"code_challenge_method", "S256"}};

    QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QString key = QJsonDocument::fromJson(data).object()["key"].toString();
        if (reply->error() != QNetworkReply::NoError || key.isEmpty())
            emit finished(QString(), AiClient::errorMessage(reply, data));
        else
            emit finished(key, QString());
    });
}
