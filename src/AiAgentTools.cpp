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

#include "AiAgentTools.h"

#include "DarkTheme.h"

#include "EditorManager.h"
#include "FolderAsWorkspaceDock.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "ScintillaNext.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include <vector>

namespace {

const int MAX_LIST_ENTRIES = 500;
const int MAX_LIST_DEPTH = 6;
const int MAX_SEARCH_FILES = 5000;
const int MAX_SEARCH_RESULTS = 200;
const qint64 MAX_FILE_SIZE = 2 * 1024 * 1024;
const int MAX_READ_CHARS = 60000;
const int MAX_COMMAND_OUTPUT = 30000;
const int COMMAND_TIMEOUT_MS = 120000;
const int DIFF_CONTEXT = 3;

bool isIgnoredDir(const QString &name)
{
    static const QStringList ignored = {
        ".git", ".svn", ".hg", "node_modules", "build", "dist", "target", ".cpm-cache",
        "__pycache__", ".venv", "venv", ".idea", ".vs", ".vscode", ".gradle", "out"
    };
    return ignored.contains(name);
}

QJsonObject schema(const QJsonObject &properties, const QStringList &required)
{
    return QJsonObject{
        {"type", "object"},
        {"properties", properties},
        {"required", QJsonArray::fromStringList(required)}
    };
}

QJsonObject prop(const QString &type, const QString &description)
{
    return QJsonObject{{"type", type}, {"description", description}};
}

QJsonObject tool(const QString &name, const QString &description, const QJsonObject &inputSchema)
{
    return QJsonObject{{"name", name}, {"description", description}, {"schema", inputSchema}};
}

// Recursively collects files below dir, skipping ignored directories.
void collectFiles(const QString &dir, int depth, int limit, QStringList *files)
{
    if (depth > MAX_LIST_DEPTH)
        return;

    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &info : entries) {
        if (files->size() >= limit)
            return;
        if (info.isDir()) {
            if (!isIgnoredDir(info.fileName()) && !info.isSymLink())
                collectFiles(info.filePath(), depth + 1, limit, files);
        }
        else {
            files->append(info.filePath());
        }
    }
}

}

AiAgentTools::AiAgentTools(MainWindow *window) :
    QObject(window),
    window(window)
{
}

QString AiAgentTools::workspaceRoot() const
{
    FolderAsWorkspaceDock *fawDock = window->findChild<FolderAsWorkspaceDock *>();
    QString root = fawDock ? fawDock->rootPath() : QString();

    if (root.isEmpty() || QDir(root).isRoot() || !QDir(root).exists()) {
        ScintillaNext *editor = window->currentEditor();
        if (editor && editor->isFile())
            root = editor->getFileInfo().absolutePath();
        else
            return QString();
    }

    return QFileInfo(root).canonicalFilePath();
}

QJsonArray AiAgentTools::definitions()
{
    return QJsonArray{
        tool("list_files",
             "List files in the workspace (or a sub folder). Ignores .git, node_modules, build output and similar folders.",
             schema({{"path", prop("string", "Folder relative to the workspace root. Default: the root.")},
                     {"recursive", prop("boolean", "List sub folders recursively. Default: true.")}}, {})),
        tool("read_file",
             "Read a text file. Returns the text currently in the editor if the file is open there (including unsaved changes).",
             schema({{"path", prop("string", "File path relative to the workspace root.")},
                     {"start_line", prop("integer", "Optional first line to read (1-based).")},
                     {"end_line", prop("integer", "Optional last line to read (inclusive).")}}, {"path"})),
        tool("search",
             "Search file contents in the workspace with a regular expression. Returns matching lines as path:line: text.",
             schema({{"pattern", prop("string", "Regular expression (case insensitive).")},
                     {"path", prop("string", "Optional sub folder to search in.")}}, {"pattern"})),
        tool("edit_file",
             "Replace an exact piece of text in a file. old_string must match the file exactly (including whitespace and indentation) "
             "and must be unique unless replace_all is true. Read the file first. The user reviews a diff before it is applied.",
             schema({{"path", prop("string", "File path relative to the workspace root.")},
                     {"old_string", prop("string", "Exact text to replace.")},
                     {"new_string", prop("string", "Replacement text.")},
                     {"replace_all", prop("boolean", "Replace every occurrence. Default: false.")}}, {"path", "old_string", "new_string"})),
        tool("write_file",
             "Create a new file or overwrite an existing one with the given content. Prefer edit_file for changes to existing files. "
             "The user reviews a diff before it is applied.",
             schema({{"path", prop("string", "File path relative to the workspace root.")},
                     {"content", prop("string", "The complete file content.")}}, {"path", "content"})),
        tool("run_command",
             "Run a shell command in the workspace root (e.g. build, tests, git). The user must approve it. "
             "Output is truncated; commands time out after 120 seconds and must not wait for input.",
             schema({{"command", prop("string", "The command line to run.")}}, {"command"})),
    };
}

AiAgentTools::Result AiAgentTools::execute(const QString &name, const QJsonObject &input, bool autoApproveEdits)
{
    if (name == "list_files")
        return listFiles(input);
    if (name == "read_file")
        return readFile(input);
    if (name == "search")
        return search(input);
    if (name == "edit_file")
        return editFile(input, autoApproveEdits);
    if (name == "write_file")
        return writeFile(input, autoApproveEdits);
    if (name == "run_command")
        return runCommand(input);

    return Result{QString("Unknown tool: %1").arg(name), tr("unknown tool"), true};
}

bool AiAgentTools::resolvePath(const QString &path, QString *absolutePath, QString *error) const
{
    const QString root = workspaceRoot();
    if (root.isEmpty()) {
        *error = "No workspace folder is open. Ask the user to open one with File > Open Folder as Workspace.";
        return false;
    }

    QString full = QDir::cleanPath(QDir(root).filePath(path.isEmpty() ? "." : path));

    // Resolve symlinks of the existing part of the path so it can't escape the workspace
    QFileInfo info(full);
    if (info.exists())
        full = info.canonicalFilePath();
    else if (QFileInfo(info.absolutePath()).exists())
        full = QFileInfo(info.absolutePath()).canonicalFilePath() + "/" + info.fileName();

    if (full != root && !full.startsWith(root + "/")) {
        *error = QString("Path '%1' is outside the workspace root.").arg(path);
        return false;
    }

    *absolutePath = full;
    return true;
}

QString AiAgentTools::relativePath(const QString &absolutePath) const
{
    return QDir(workspaceRoot()).relativeFilePath(absolutePath);
}

bool AiAgentTools::readText(const QString &absolutePath, QString *text, QString *error) const
{
    NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);
    ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(absolutePath);
    if (editor) {
        *text = QString::fromUtf8(editor->getText(editor->textLength() + 1));
        return true;
    }

    QFile file(absolutePath);
    if (file.size() > MAX_FILE_SIZE) {
        *error = "File is too large.";
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QString("Cannot open '%1': %2").arg(relativePath(absolutePath), file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.left(8192).contains('\0')) {
        *error = "File looks binary.";
        return false;
    }
    *text = QString::fromUtf8(data);
    return true;
}

AiAgentTools::Result AiAgentTools::listFiles(const QJsonObject &input)
{
    QString dir, error;
    if (!resolvePath(input["path"].toString(), &dir, &error))
        return Result{error, error, true};
    if (!QFileInfo(dir).isDir())
        return Result{"Not a folder.", tr("not a folder"), true};

    QStringList lines;
    if (input["recursive"].toBool(true)) {
        QStringList files;
        collectFiles(dir, 0, MAX_LIST_ENTRIES, &files);
        for (const QString &f : files)
            lines.append(relativePath(f));
        if (files.size() >= MAX_LIST_ENTRIES)
            lines.append(QString("... (stopped after %1 entries, list a sub folder for more)").arg(MAX_LIST_ENTRIES));
    }
    else {
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name | QDir::DirsFirst);
        for (const QFileInfo &info : entries)
            lines.append(relativePath(info.filePath()) + (info.isDir() ? "/" : ""));
    }

    if (lines.isEmpty())
        return Result{"(empty)", tr("empty folder"), false};
    return Result{lines.join('\n'), tr("%1 entries").arg(lines.size()), false};
}

AiAgentTools::Result AiAgentTools::readFile(const QJsonObject &input)
{
    QString path, error, text;
    if (!resolvePath(input["path"].toString(), &path, &error) || !readText(path, &text, &error))
        return Result{error, error, true};

    QStringList lines = text.split('\n');
    const int total = lines.size();
    const int start = qBound(1, input["start_line"].toInt(1), total);
    const int end = qBound(start, input["end_line"].toInt(total), total);
    QString content = lines.mid(start - 1, end - start + 1).join('\n');

    QString header = QString("%1 (%2 lines").arg(relativePath(path)).arg(total);
    if (start != 1 || end != total)
        header += QString(", showing %1-%2").arg(start).arg(end);
    header += ")\n";

    if (content.size() > MAX_READ_CHARS) {
        content = content.left(MAX_READ_CHARS) + "\n... (truncated, use start_line/end_line to read the rest)";
    }
    return Result{header + content, tr("%1 lines").arg(end - start + 1), false};
}

AiAgentTools::Result AiAgentTools::search(const QJsonObject &input)
{
    QString dir, error;
    if (!resolvePath(input["path"].toString(), &dir, &error))
        return Result{error, error, true};

    const QRegularExpression re(input["pattern"].toString(), QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid())
        return Result{"Invalid regular expression: " + re.errorString(), tr("invalid pattern"), true};

    QStringList files;
    if (QFileInfo(dir).isDir())
        collectFiles(dir, 0, MAX_SEARCH_FILES, &files);
    else
        files.append(dir);

    QStringList results;
    for (const QString &f : files) {
        QString text;
        if (QFileInfo(f).size() > MAX_FILE_SIZE || !readText(f, &text, &error))
            continue;
        const QStringList lines = text.split('\n');
        for (int i = 0; i < lines.size() && results.size() < MAX_SEARCH_RESULTS; ++i) {
            if (re.match(lines[i]).hasMatch())
                results.append(QString("%1:%2: %3").arg(relativePath(f)).arg(i + 1).arg(lines[i].trimmed().left(200)));
        }
        if (results.size() >= MAX_SEARCH_RESULTS) {
            results.append("... (too many matches, refine the pattern)");
            break;
        }
    }

    if (results.isEmpty())
        return Result{"No matches.", tr("no matches"), false};
    return Result{results.join('\n'), tr("%1 matches").arg(results.size()), false};
}

AiAgentTools::Result AiAgentTools::editFile(const QJsonObject &input, bool autoApprove)
{
    QString path, error, text;
    if (!resolvePath(input["path"].toString(), &path, &error) || !readText(path, &text, &error))
        return Result{error, error, true};

    QString oldString = input["old_string"].toString();
    QString newString = input["new_string"].toString();
    if (oldString.isEmpty())
        return Result{"old_string must not be empty. Use write_file to create files.", tr("empty old_string"), true};

    // Models usually send \n; match files that use Windows line endings
    if (text.contains("\r\n") && !oldString.contains("\r\n")) {
        oldString.replace("\n", "\r\n");
        newString.replace("\n", "\r\n");
    }

    const int count = text.count(oldString);
    if (count == 0)
        return Result{"old_string was not found in the file. Read the file again and copy the text exactly.", tr("text not found"), true};
    if (count > 1 && !input["replace_all"].toBool())
        return Result{QString("old_string occurs %1 times. Add more surrounding lines to make it unique, or set replace_all.").arg(count), tr("text not unique"), true};

    QString newText = text;
    if (input["replace_all"].toBool())
        newText.replace(oldString, newString);
    else
        newText.replace(newText.indexOf(oldString), oldString.size(), newString);

    return applyChange(path, text, newText, false, autoApprove);
}

AiAgentTools::Result AiAgentTools::writeFile(const QJsonObject &input, bool autoApprove)
{
    QString path, error, oldText;
    if (!resolvePath(input["path"].toString(), &path, &error))
        return Result{error, error, true};

    const bool exists = QFileInfo::exists(path);
    if (exists && QFileInfo(path).isDir())
        return Result{"Path is a folder.", tr("path is a folder"), true};
    if (exists && !readText(path, &oldText, &error))
        return Result{error, error, true};

    return applyChange(path, oldText, input["content"].toString(), !exists, autoApprove);
}

AiAgentTools::Result AiAgentTools::applyChange(const QString &absolutePath, const QString &oldText, const QString &newText, bool isNew, bool autoApprove)
{
    const QString rel = relativePath(absolutePath);

    if (oldText == newText)
        return Result{"No changes: the new content is identical.", tr("no changes"), false};

    if (!autoApprove) {
        const QString title = isNew ? tr("Create file: %1").arg(rel) : tr("Edit file: %1").arg(rel);
        if (!askApproval(window, title, diffHtml(oldText, newText)))
            return Result{"The user rejected this change. Ask what they want instead.", tr("rejected by user"), true};
    }

    if (beforeChange)
        beforeChange(absolutePath, !isNew, oldText);

    const QString error = writeContent(absolutePath, newText, true);
    if (!error.isEmpty())
        return Result{error, tr("write failed"), true};

    return Result{QString("%1 %2.").arg(isNew ? "Created" : "Updated", rel), isNew ? tr("created") : tr("applied"), false};
}

QString AiAgentTools::writeContent(const QString &absolutePath, const QString &text, bool openIfClosed)
{
    NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);
    ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(absolutePath);

    if (editor) {
        // Edit the open buffer so the change can be undone with Ctrl+Z, then save it
        const QByteArray data = text.toUtf8();
        editor->targetWholeDocument();
        editor->replaceTargetMinimal(data.size(), data.constData());
        if (editor->save() != QFileDevice::NoError)
            return "The change was applied in the editor but saving failed.";
        return QString();
    }

    QDir().mkpath(QFileInfo(absolutePath).absolutePath());
    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit())
        return QString("Cannot write '%1': %2").arg(relativePath(absolutePath), file.errorString());
    if (openIfClosed)
        window->openFile(absolutePath);
    return QString();
}

bool AiAgentTools::restoreFile(const QString &absolutePath, bool existed, const QString &content)
{
    if (existed)
        return writeContent(absolutePath, content, false).isEmpty();

    NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);
    ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(absolutePath);
    if (editor) {
        // Closing a file that was just saved does not prompt
        editor->close();
    }
    return QFile::remove(absolutePath) || !QFileInfo::exists(absolutePath);
}

AiAgentTools::Result AiAgentTools::runCommand(const QJsonObject &input)
{
    const QString command = input["command"].toString().trimmed();
    const QString root = workspaceRoot();
    if (command.isEmpty())
        return Result{"Empty command.", tr("empty command"), true};
    if (root.isEmpty())
        return Result{"No workspace folder is open.", tr("no workspace"), true};

    const QString html = QString("<p>%1</p><pre style=\"font-family: monospace; font-size: 11pt;\">%2</pre><p style=\"color: gray;\">%3</p>")
                             .arg(tr("The AI wants to run this command:"), command.toHtmlEscaped(), tr("in %1").arg(root.toHtmlEscaped()));
    if (!askApproval(window, tr("Run command"), html))
        return Result{"The user rejected this command.", tr("rejected by user"), true};

    QProcess process;
    process.setWorkingDirectory(root);
    process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    process.setNativeArguments("/c " + command);
    process.start("cmd.exe", QStringList());
#else
    process.start("/bin/sh", {"-c", command});
#endif
    process.closeWriteChannel();

    if (!process.waitForStarted(5000))
        return Result{"Failed to start: " + process.errorString(), tr("failed to start"), true};

    // Keep the UI responsive while the command runs
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(COMMAND_TIMEOUT_MS);
    if (process.state() != QProcess::NotRunning)
        loop.exec();

    bool timedOut = false;
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(2000);
        timedOut = true;
    }

    QString output = QString::fromUtf8(process.readAll());
    if (output.size() > MAX_COMMAND_OUTPUT)
        output = "... (output truncated)\n" + output.right(MAX_COMMAND_OUTPUT);

    if (timedOut)
        return Result{"Command timed out after 120 seconds.\n" + output, tr("timed out"), true};

    const int code = process.exitCode();
    return Result{QString("Exit code: %1\n%2").arg(code).arg(output), tr("exit code %1").arg(code), code != 0};
}

bool AiAgentTools::askApproval(QWidget *parent, const QString &title, const QString &html)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(900, 600);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *label = new QLabel(QString("<b>%1</b>").arg(title.toHtmlEscaped()), &dialog);
    layout->addWidget(label);

    QTextBrowser *view = new QTextBrowser(&dialog);
    view->setHtml(html);
    layout->addWidget(view, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
    QPushButton *apply = buttons->addButton(tr("Accept"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Reject"), QDialogButtonBox::RejectRole);
    apply->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    return dialog.exec() == QDialog::Accepted;
}

QString AiAgentTools::diffHtml(const QString &oldText, const QString &newText)
{
    const QStringList a = oldText.isEmpty() ? QStringList() : QString(oldText).remove('\r').split('\n');
    const QStringList b = newText.isEmpty() ? QStringList() : QString(newText).remove('\r').split('\n');

    // Lines shared at the start and end are unchanged; only diff the middle
    int prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix])
        ++prefix;
    int suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix && a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
        ++suffix;

    const QStringList midA = a.mid(prefix, a.size() - prefix - suffix);
    const QStringList midB = b.mid(prefix, b.size() - prefix - suffix);

    std::vector<std::pair<QChar, QString>> ops;
    for (int i = 0; i < prefix; ++i)
        ops.push_back({' ', a[i]});

    const qsizetype n = midA.size(), m = midB.size();
    if (n * m <= 4000000) {
        // Longest common subsequence, lengths computed from the end
        std::vector<int> lcs((n + 1) * (m + 1), 0);
        auto at = [&](qsizetype i, qsizetype j) -> int & { return lcs[i * (m + 1) + j]; };
        for (qsizetype i = n - 1; i >= 0; --i)
            for (qsizetype j = m - 1; j >= 0; --j)
                at(i, j) = midA[i] == midB[j] ? at(i + 1, j + 1) + 1 : qMax(at(i + 1, j), at(i, j + 1));

        qsizetype i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && midA[i] == midB[j]) {
                ops.push_back({' ', midA[i++]});
                ++j;
            }
            else if (i < n && (j == m || at(i + 1, j) >= at(i, j + 1))) {
                ops.push_back({'-', midA[i++]});
            }
            else {
                ops.push_back({'+', midB[j++]});
            }
        }
    }
    else {
        for (const QString &line : midA)
            ops.push_back({'-', line});
        for (const QString &line : midB)
            ops.push_back({'+', line});
    }

    for (int i = a.size() - suffix; i < a.size(); ++i)
        ops.push_back({' ', a[i]});

    // Only show changed lines plus a few lines of context around them
    const int count = static_cast<int>(ops.size());
    std::vector<bool> visible(count, false);
    int added = 0, removed = 0;
    for (int i = 0; i < count; ++i) {
        if (ops[i].first == ' ')
            continue;
        ops[i].first == '+' ? ++added : ++removed;
        for (int k = qMax(0, i - DIFF_CONTEXT); k <= qMin(count - 1, i + DIFF_CONTEXT); ++k)
            visible[k] = true;
    }

    const bool dark = DarkTheme::isEnabled();
    const QString addedStyle = dark ? "background-color: #1f3d2b; color: #b8e6c4;" : "background-color: #ccffd8; color: #000000;";
    const QString removedStyle = dark ? "background-color: #4a2125; color: #ffc9c9;" : "background-color: #ffd7d5; color: #000000;";

    QString html = QString("<p><span style=\"color: #3fb950;\">+%1</span> &nbsp; <span style=\"color: #f85149;\">-%2</span></p>")
                       .arg(added).arg(removed);
    html += "<pre style=\"font-family: monospace;\">";
    bool skipped = false;
    for (int i = 0; i < count; ++i) {
        if (!visible[i]) {
            skipped = true;
            continue;
        }
        if (skipped) {
            html += "<span style=\"color: gray;\">   ...</span>\n";
            skipped = false;
        }
        const QString line = ops[i].second.toHtmlEscaped();
        if (ops[i].first == '+')
            html += QString("<span style=\"%1\">+ %2</span>\n").arg(addedStyle, line);
        else if (ops[i].first == '-')
            html += QString("<span style=\"%1\">- %2</span>\n").arg(removedStyle, line);
        else
            html += QString("  %1\n").arg(line);
    }
    html += "</pre>";
    return html;
}
