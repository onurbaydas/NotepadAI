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

#include <QFrame>
#include <QPointer>
#include <QStringList>

class MainWindow;
class QAction;
class QLineEdit;
class QListWidget;

// The popup at the top of the window behind Ctrl+P (go to file, "file:42",
// ":42" for a line) and Ctrl+Shift+P (a ">" query lists every menu command)
class QuickOpen : public QFrame
{
    Q_OBJECT

public:
    explicit QuickOpen(MainWindow *window);

    void showFiles();
    void showCommands();

    // Fuzzy match score of query in text, -1 when not all characters are found in order
    static int fuzzyScore(const QString &query, const QString &text);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void popup(const QString &text);
    void collectFiles();
    void collectCommands();
    void updateResults();
    void acceptCurrent();

    MainWindow *window;
    QLineEdit *input;
    QListWidget *list;

    QString root;
    QStringList files;
    QList<QPair<QString, QPointer<QAction>>> commands;
};
