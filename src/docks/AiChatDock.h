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
#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QPointer>

class AiAgentTools;
class MainWindow;
class QCheckBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
class QUrl;

// Dockable AI assistant that talks to Anthropic (Claude) or any
// OpenAI-compatible endpoint (OpenAI, Ollama, LM Studio, OpenRouter...).
// In agent mode the model can call tools to read, search and edit files
// in the workspace and run commands, with the user approving changes.
class AiChatDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AiChatDock(MainWindow *window);

    void focusInput();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void sendMessage();
    void stopRequest();
    void newChat();
    void showHistoryMenu();
    void insertLastCodeBlock();
    void explainSelection();
    void linkClicked(const QUrl &url);

private:
    struct PendingToolCall {
        QString id;
        QString name;
        QString arguments;  // JSON, streamed in pieces
    };

    // Original state of a file before the agent first changed it in a turn
    using FileSnapshot = QPair<bool, QString>;  // existed, content

    QString buildContext() const;
    QString systemPrompt() const;
    void addUserMessage(const QString &question);
    void sendRequest();
    void compactHistory();
    QJsonArray anthropicMessages() const;
    QJsonArray openAiMessages() const;
    void handleStreamData();
    void handleStreamLine(const QByteArray &line);
    void requestFinished();
    void runToolCalls();
    void restoreCheckpoint(int turn);
    void renderChat();
    void setBusy(bool busy);

    QString chatsDirectory() const;
    void saveChat();
    void loadChat(const QString &filePath);

    MainWindow *window;
    AiAgentTools *tools;
    QNetworkAccessManager *network;
    QPointer<QNetworkReply> reply;
    QByteArray streamBuffer;

    QTextBrowser *chatView;
    QPlainTextEdit *input;
    QCheckBox *includeContext;
    QCheckBox *agentMode;
    QCheckBox *autoApprove;
    QPushButton *sendButton;
    QPushButton *clearButton;
    QPushButton *historyButton;
    QPushButton *stopButton;
    QPushButton *insertButton;
    QLabel *statusLabel;

    // Provider-neutral conversation:
    //   {role: "user", content, display, turn}
    //   {role: "assistant", content, tool_calls: [{id, name, input}]}
    //   {role: "tool", tool_call_id, name, label, content, summary, is_error}
    QJsonArray history;
    QString chatId;
    int nextTurn = 1;
    bool historyTrimmed = false;
    QMap<int, QMap<QString, FileSnapshot>> checkpoints;  // turn -> path -> original

    QString pendingAnswer;
    QMap<int, PendingToolCall> pendingToolCalls;
    QString lastError;
    QString usageText;
    bool stopRequested = false;
    bool runningTools = false;
    int agentSteps = 0;
};
