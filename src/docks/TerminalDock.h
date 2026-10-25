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
#include <QStringList>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QToolButton;

// A simple integrated terminal: each command runs in the system shell
// (cmd on Windows, bash elsewhere) in the current folder, output streams in.
// While a command runs, typed lines go to its input.
class TerminalDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit TerminalDock(QWidget *parent = nullptr);

    void setWorkingDirectory(const QString &dir);
    QString workingDirectory() const { return cwd; }

    void runCommand(const QString &command);
    void focusInput();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void submit();
    void startProcess(const QString &command);
    void stopProcess();
    void readOutput();
    void processFinished(int exitCode);
    void append(const QString &text, const QColor &colour = QColor());
    void updatePrompt();
    void refreshStyle();

    QPlainTextEdit *output;
    QLineEdit *input;
    QLabel *prompt;
    QLabel *status;
    QToolButton *stopButton;
    QProcess *process = nullptr;

    QString cwd;
    QStringList history;
    int historyIndex = 0;
};
