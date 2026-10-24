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

#include <QDockWidget>
#include <QJsonArray>
#include <QPointer>

class MainWindow;
class QCheckBox;
class QComboBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

// Dockable chat panel that talks to Anthropic (Claude) or any
// OpenAI-compatible endpoint (OpenAI, Ollama, LM Studio, OpenRouter...).
class AiChatDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AiChatDock(MainWindow *window);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void sendMessage();
    void stopRequest();
    void clearChat();
    void openSettings();
    void insertLastCodeBlock();
    void explainSelection();

private:
    struct Provider {
        QString type;     // "anthropic" or "openai"
        QString baseUrl;
        QString apiKey;
        QString model;
    };

    Provider loadProvider() const;
    QString buildContext() const;
    void startRequest(const QString &prompt);
    void handleStreamData();
    void handleStreamLine(const QByteArray &line);
    void requestFinished();
    void renderChat();
    void setBusy(bool busy);

    MainWindow *window;
    QNetworkAccessManager *network;
    QPointer<QNetworkReply> reply;
    QByteArray streamBuffer;

    QTextBrowser *chatView;
    QPlainTextEdit *input;
    QCheckBox *includeContext;
    QPushButton *sendButton;
    QPushButton *stopButton;
    QPushButton *insertButton;
    QLabel *statusLabel;

    QJsonArray history;     // [{role, content}, ...]
    QString pendingAnswer;  // assistant text currently streaming in
    QString lastError;
};
