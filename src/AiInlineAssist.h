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

#include <QObject>
#include <QPointer>

class AiClient;
class MainWindow;
class QAction;
class QNetworkReply;
class QTimer;
class ScintillaNext;

// AI features that live in the editor itself:
//  - inline edit: select code, press Ctrl+I, describe the change, review the diff
//  - autocomplete: grey "ghost text" suggestion after a typing pause, Tab accepts
class AiInlineAssist : public QObject
{
    Q_OBJECT

public:
    explicit AiInlineAssist(MainWindow *window);

    QAction *inlineEditAction() const { return editAction; }
    QAction *autocompleteAction() const { return completeAction; }

    void watchEditor(ScintillaNext *editor);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void inlineEdit();

    void scheduleCompletion(ScintillaNext *editor);
    void requestCompletion();
    void showSuggestion(ScintillaNext *editor, int pos, const QString &text);
    void acceptSuggestion();
    void dismissSuggestion();

    MainWindow *window;
    AiClient *client;
    QAction *editAction;
    QAction *completeAction;

    QTimer *debounce;
    QPointer<ScintillaNext> typingEditor;
    QPointer<QNetworkReply> completionReply;
    int generation = 0;

    QPointer<ScintillaNext> ghostEditor;
    int ghostPos = -1;
    int ghostLine = -1;
    QString ghostText;
};
