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
class AiChatView;
class AiReviewManager;
class QListWidget;
class QVBoxLayout;
class MainWindow;
class QComboBox;
class QFrame;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QTimer;
class QToolButton;

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
    void explainSelection();
    void setReviewManager(AiReviewManager *manager);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void sendMessage();
    void stopRequest();
    void newChat();
    void showHistoryMenu();
    void insertCode(const QString &code);
    void openFile(const QString &path);

private:
    struct PendingToolCall {
        QString id;
        QString name;
        QString arguments;  // JSON, streamed in pieces
    };

    // Original state of a file before the agent first changed it in a turn
    using FileSnapshot = QPair<bool, QString>;  // existed, content

    QWidget *createTitleBar();
    QFrame *createComposer();
    QFrame *createReviewStrip();
    void updateReviewStrip();
    void updateMentionPopup();
    void insertMention(const QString &path);
    QString mentionedFiles(const QString &question) const;
    QString projectRules() const;
    void updateContextChip();
    void updateComposerHeight();
    void updateModelButton();
    bool isAgentMode() const;

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
    void updateLive();
    void setBusy(bool busy);

    QString chatsDirectory() const;
    void saveChat();
    void loadChat(const QString &filePath);

    MainWindow *window;
    AiAgentTools *tools;
    QNetworkAccessManager *network;
    QPointer<QNetworkReply> reply;
    QByteArray streamBuffer;

    AiChatView *chatView;
    QPlainTextEdit *input;
    QComboBox *modeCombo;
    QToolButton *modelButton;
    QToolButton *sendButton;
    QToolButton *newChatButton;
    QToolButton *historyButton;
    QLabel *statusLabel;
    QLabel *contextChip;
    QToolButton *contextToggle;
    QTimer *contextTimer;
    QListWidget *mentionList;
    QStringList mentionCandidates;
    AiReviewManager *reviews = nullptr;
    QFrame *reviewStrip;
    QLabel *reviewTitle;
    QVBoxLayout *reviewFiles;
    bool contextEnabled = true;

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
    QString currentTool;
};
