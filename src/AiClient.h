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

#pragma once

#include <QNetworkRequest>
#include <QObject>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QWidget;

// Shared AI provider settings and requests used by the chat dock,
// inline edit and autocomplete.
class AiClient : public QObject
{
    Q_OBJECT

public:
    struct Provider {
        QString type;            // "anthropic" or "openai" (any OpenAI-compatible endpoint)
        QString baseUrl;
        QString apiKey;
        QString model;
        QString completionModel; // smaller/faster model for autocomplete; empty = model
    };

    using Callback = std::function<void(const QString &text, const QString &error)>;

    explicit AiClient(QObject *parent = nullptr);

    static Provider load();
    static bool isConfigured(const Provider &p);

    // Endpoint URL and auth headers for a chat request
    static QNetworkRequest buildRequest(const Provider &p);
    static QString errorMessage(QNetworkReply *reply, const QByteArray &body);

    // Settings dialog with the "Sign in with OpenRouter" browser login. Returns true if saved.
    static bool showSettings(QWidget *parent);

    // Non-streaming single request. The callback is not called if the reply is aborted.
    QNetworkReply *complete(const QString &system, const QString &user, int maxTokens, bool useCompletionModel, const Callback &done);

    QNetworkAccessManager *network() const { return manager; }

private:
    QNetworkAccessManager *manager;
};

// OAuth PKCE login: opens the browser, catches the redirect on localhost and
// exchanges the code for an OpenRouter API key.
class OpenRouterLogin : public QObject
{
    Q_OBJECT

public:
    explicit OpenRouterLogin(QObject *parent = nullptr);
    void start();

signals:
    void finished(const QString &apiKey, const QString &error);

private:
    void exchangeCode(const QString &code);

    QByteArray verifier;
    QNetworkAccessManager *manager;
};
