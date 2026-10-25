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

#include "TerminalDock.h"

#include "DarkTheme.h"
#include "UiIcons.h"

#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString stripAnsi(QString text)
{
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;?]*[ -/]*[@-~]|\x1b\\][^\x07]*\x07|\x1b[()][A-Za-z0-9]"));
    text.remove(ansi);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.remove(QChar('\r'));
    return text;
}

}

TerminalDock::TerminalDock(QWidget *parent) :
    QDockWidget(parent)
{
    setObjectName("TerminalDock");
    setWindowTitle(tr("Terminal"));

    // Title bar
    QWidget *bar = new QWidget(this);
    bar->setObjectName("dockTitleBar");
    QHBoxLayout *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(10, 3, 4, 3);
    barLayout->setSpacing(0);
    QLabel *title = new QLabel(tr("TERMINAL"), bar);
    title->setObjectName("dockTitle");
    status = new QLabel(bar);
    status->setObjectName("dockStatus");
    barLayout->addWidget(title);
    barLayout->addSpacing(10);
    barLayout->addWidget(status, 1);

    stopButton = new QToolButton(bar);
    stopButton->setText(QStringLiteral("■"));
    stopButton->setToolTip(tr("Stop the running command (Ctrl+C)"));
    stopButton->setAutoRaise(true);
    stopButton->setEnabled(false);
    connect(stopButton, &QToolButton::clicked, this, &TerminalDock::stopProcess);

    QToolButton *clear = new QToolButton(bar);
    clear->setIcon(UiIcons::glyph(UiIcons::Trash));
    clear->setToolTip(tr("Clear"));
    clear->setAutoRaise(true);

    QToolButton *close = new QToolButton(bar);
    close->setIcon(UiIcons::glyph(UiIcons::Close));
    close->setToolTip(tr("Close"));
    close->setAutoRaise(true);
    connect(close, &QToolButton::clicked, this, &QDockWidget::close);

    barLayout->addWidget(stopButton);
    barLayout->addWidget(clear);
    barLayout->addWidget(close);
    setTitleBarWidget(bar);

    // Output and input
    QWidget *content = new QWidget(this);
    content->setObjectName("terminalContent");
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 4, 8, 6);
    layout->setSpacing(2);

    output = new QPlainTextEdit(content);
    output->setObjectName("terminalOutput");
    output->setReadOnly(true);
    output->setFrameShape(QFrame::NoFrame);
    output->setMaximumBlockCount(5000);
    output->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    output->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    connect(clear, &QToolButton::clicked, output, &QPlainTextEdit::clear);

    QHBoxLayout *inputLayout = new QHBoxLayout;
    inputLayout->setSpacing(6);
    prompt = new QLabel(content);
    prompt->setObjectName("terminalPrompt");
    input = new QLineEdit(content);
    input->setObjectName("terminalInput");
    input->setFrame(false);
    input->setPlaceholderText(tr("Type a command and press Enter"));
    input->installEventFilter(this);
    connect(input, &QLineEdit::returnPressed, this, &TerminalDock::submit);
    inputLayout->addWidget(prompt);
    inputLayout->addWidget(input, 1);

    layout->addWidget(output, 1);
    layout->addLayout(inputLayout);
    setWidget(content);

    const QFont font = DarkTheme::monospaceFont();
    output->setFont(font);
    input->setFont(font);
    prompt->setFont(font);

    // Clicking the output puts the caret back in the input
    output->viewport()->installEventFilter(this);

    setWorkingDirectory(QDir::homePath());
    refreshStyle();
}

void TerminalDock::refreshStyle()
{
    const bool dark = DarkTheme::isEnabled();
    setStyleSheet(QStringLiteral(
        "#terminalContent, QPlainTextEdit#terminalOutput, QLineEdit#terminalInput { background-color: %1; color: %2; }"
        "QLabel#terminalPrompt { color: %3; font-weight: bold; }")
        .arg(dark ? "#1e1f22" : "#ffffff", dark ? "#cfd2d6" : "#24292f", dark ? "#5b93f5" : "#0969da"));
}

void TerminalDock::setWorkingDirectory(const QString &dir)
{
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return;

    cwd = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    updatePrompt();
}

void TerminalDock::updatePrompt()
{
    QString name = QFileInfo(cwd).fileName();
    if (name.isEmpty())
        name = QDir::toNativeSeparators(cwd);
    prompt->setText(name + QStringLiteral(" ›"));
    prompt->setToolTip(QDir::toNativeSeparators(cwd));

    const bool running = process && process->state() != QProcess::NotRunning;
    status->setText(running ? tr("running...") : QDir::toNativeSeparators(cwd));
    stopButton->setEnabled(running);
    input->setPlaceholderText(running ? tr("Input for the running command") : tr("Type a command and press Enter"));
}

void TerminalDock::focusInput()
{
    input->setFocus();
}

void TerminalDock::runCommand(const QString &command)
{
    if (!isVisible())
        show();
    raise();

    if (process && process->state() != QProcess::NotRunning) {
        append(tr("A command is still running. Stop it first.") + '\n', QColor(0xe0, 0x6c, 0x6c));
        return;
    }

    input->setText(command);
    submit();
}

bool TerminalDock::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == output->viewport() && event->type() == QEvent::MouseButtonRelease) {
        if (!output->textCursor().hasSelection())
            input->setFocus();
        return false;
    }

    // Ctrl+C and Ctrl+L belong to the terminal while its input has focus, not to the window shortcuts
    if (obj == input && event->type() == QEvent::ShortcutOverride) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->modifiers() == Qt::ControlModifier && (keyEvent->key() == Qt::Key_L || (keyEvent->key() == Qt::Key_C && !input->hasSelectedText()))) {
            event->accept();
            return true;
        }
    }

    if (obj != input || event->type() != QEvent::KeyPress)
        return QDockWidget::eventFilter(obj, event);

    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

    if (keyEvent->key() == Qt::Key_C && keyEvent->modifiers() == Qt::ControlModifier && !input->hasSelectedText()) {
        if (process && process->state() != QProcess::NotRunning) {
            stopProcess();
            return true;
        }
    }
    else if (keyEvent->key() == Qt::Key_L && keyEvent->modifiers() == Qt::ControlModifier) {
        output->clear();
        return true;
    }
    else if (keyEvent->key() == Qt::Key_Up && !history.isEmpty()) {
        historyIndex = qMax(0, historyIndex - 1);
        input->setText(history.value(historyIndex));
        return true;
    }
    else if (keyEvent->key() == Qt::Key_Down && !history.isEmpty()) {
        historyIndex = qMin(history.size(), historyIndex + 1);
        input->setText(history.value(historyIndex));
        return true;
    }

    return QDockWidget::eventFilter(obj, event);
}

void TerminalDock::submit()
{
    const QString command = input->text();
    input->clear();

    // Input for a running program (for example a Python input() prompt)
    if (process && process->state() != QProcess::NotRunning) {
        append(command + '\n');
        process->write(command.toUtf8() + '\n');
        return;
    }

    const QString trimmed = command.trimmed();
    append(QFileInfo(cwd).fileName() + QStringLiteral(" › "), QColor(0x5b, 0x93, 0xf5));
    append(trimmed + '\n');

    if (trimmed.isEmpty())
        return;

    if (history.isEmpty() || history.last() != trimmed)
        history.append(trimmed);
    historyIndex = history.size();

    if (trimmed == QLatin1String("cls") || trimmed == QLatin1String("clear")) {
        output->clear();
        return;
    }

    // cd changes the terminal's folder; each command runs in a fresh shell
    static const QRegularExpression cdPattern(QStringLiteral("^(?:cd|chdir)(?:\\s+/d)?(?:\\s+(.*))?$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch cd = cdPattern.match(trimmed);
    if (cd.hasMatch()) {
        QString target = cd.captured(1).trimmed();
        if (target.size() >= 2 && target.startsWith('"') && target.endsWith('"'))
            target = target.mid(1, target.size() - 2);
        if (target.isEmpty()) {
#ifdef Q_OS_WIN
            append(QDir::toNativeSeparators(cwd) + '\n');
            return;
#else
            target = QDir::homePath();
#endif
        }
        if (target.startsWith('~'))
            target = QDir::homePath() + target.mid(1);

        const QString resolved = QDir(cwd).absoluteFilePath(target);
        if (QFileInfo(resolved).isDir())
            setWorkingDirectory(resolved);
        else
            append(tr("Folder not found: %1").arg(target) + '\n', QColor(0xe0, 0x6c, 0x6c));
        return;
    }

    startProcess(trimmed);
}

void TerminalDock::startProcess(const QString &command)
{
    if (!process) {
        process = new QProcess(this);
        process->setProcessChannelMode(QProcess::MergedChannels);
        connect(process, &QProcess::readyReadStandardOutput, this, &TerminalDock::readOutput);
        connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) { processFinished(exitCode); });
        connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                append(tr("Could not start the shell: %1").arg(process->errorString()) + '\n', QColor(0xe0, 0x6c, 0x6c));
                updatePrompt();
            }
        });
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Plain, unbuffered UTF-8 output from common tools
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    env.insert("NO_COLOR", "1");
    env.insert("TERM", "dumb");
    process->setProcessEnvironment(env);
    process->setWorkingDirectory(cwd);

#ifdef Q_OS_WIN
    process->setProgram("cmd.exe");
    process->setArguments({});
    process->setNativeArguments(QStringLiteral("/d /s /c \"chcp 65001>nul & %1\"").arg(command));
#else
    const QString bash = QStandardPaths::findExecutable("bash");
    process->setProgram(bash.isEmpty() ? QStringLiteral("/bin/sh") : bash);
    process->setArguments({"-c", command});
#endif

    process->start();
    updatePrompt();
}

void TerminalDock::stopProcess()
{
    if (!process || process->state() == QProcess::NotRunning)
        return;

#ifdef Q_OS_WIN
    // Kill the whole tree: cmd.exe and the program it started
    QProcess::execute("taskkill", {"/F", "/T", "/PID", QString::number(process->processId())});
#endif
    process->kill();
    append(QStringLiteral("^C\n"), QColor(0xe0, 0x6c, 0x6c));
}

void TerminalDock::readOutput()
{
    static QStringDecoder decoder(QStringDecoder::Utf8);
    const QByteArray data = process->readAllStandardOutput();
    append(stripAnsi(decoder.decode(data)));
}

void TerminalDock::processFinished(int exitCode)
{
    readOutput();

    // Make sure the next prompt starts on a new line
    if (!output->toPlainText().endsWith('\n') && !output->toPlainText().isEmpty())
        append(QStringLiteral("\n"));

    if (exitCode != 0)
        append(tr("exit code %1").arg(exitCode) + '\n', QColor(0xe0, 0x6c, 0x6c));

    updatePrompt();
}

void TerminalDock::append(const QString &text, const QColor &colour)
{
    if (text.isEmpty())
        return;

    QScrollBar *scrollBar = output->verticalScrollBar();
    const bool atBottom = scrollBar->value() >= scrollBar->maximum() - 4;

    QTextCursor cursor(output->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    if (colour.isValid())
        format.setForeground(colour);
    cursor.insertText(text, format);

    if (atBottom)
        scrollBar->setValue(scrollBar->maximum());
}
