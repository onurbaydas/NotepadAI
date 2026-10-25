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

#include <QJsonArray>
#include <QMap>
#include <QScrollArea>

class QEventLoop;
class QFrame;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

// Renders the AI conversation as a list of widgets: user bubbles, answer
// text, code blocks with copy/insert buttons and one-line tool steps.
class AiChatView : public QScrollArea
{
    Q_OBJECT

public:
    explicit AiChatView(QWidget *parent = nullptr);

    // history uses the provider-neutral format of AiChatDock;
    // checkpointFiles maps a user turn to the number of files that can be restored
    void setConversation(const QJsonArray &history, const QMap<int, int> &checkpointFiles, bool busy, bool trimmed);

    // The answer currently streaming in
    void setLive(bool active, const QString &text = QString(), const QStringList &pendingTools = QStringList());
    void setError(const QString &error);

    // Shows a card with two buttons in the chat and waits until one is clicked (or cancelApproval() is called)
    bool waitForApproval(const QString &title, const QString &detail, const QString &okText, const QString &cancelText);
    void cancelApproval();

    // Minimal Markdown to Qt rich text (paragraphs, headings, lists, bold, italic, inline code, links, fenced code)
    static QString markdownToHtml(const QString &markdown);

    void refreshStyle();

signals:
    void restoreRequested(int turn);
    void insertCodeRequested(const QString &code);
    void openFileRequested(const QString &path);
    void suggestionClicked(const QString &prompt);

private:
    void clearMessages();
    void addEmptyState();
    void addUserMessage(const QString &text, int turn, int restorableFiles, bool busy);
    void addAssistantText(const QString &markdown);
    void addToolStep(const QJsonObject &message);
    QLabel *makeTextLabel(const QString &html);
    void scrollToBottomLater();
    bool isAtBottom() const;

    QWidget *container;
    QVBoxLayout *messages;
    QWidget *liveBox;
    QLabel *liveText;
    QLabel *liveStatus;
    QLabel *errorLabel;
    QFrame *approvalCard;
    QLabel *approvalTitle;
    QLabel *approvalDetail;
    QPushButton *approvalOk;
    QPushButton *approvalCancel;
    QEventLoop *approvalLoop = nullptr;
    bool approvalResult = false;
    QTimer *dots;
    int dotCount = 0;
    QString liveStatusText;
};
