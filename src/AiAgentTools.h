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
#include <QJsonObject>
#include <QObject>

#include <functional>

class MainWindow;
class QWidget;

// The tools the AI agent can call: explore the workspace, edit files
// (after the user approves a diff) and run shell commands (after approval).
class AiAgentTools : public QObject
{
    Q_OBJECT

public:
    struct Result {
        QString content;   // sent back to the model
        QString summary;   // one line shown in the chat
        bool isError = false;
    };

    explicit AiAgentTools(MainWindow *window);

    // Folder opened with "Open Folder as Workspace", else the current file's folder.
    QString workspaceRoot() const;

    // Tool definitions in a provider-neutral form: [{name, description, schema}]
    static QJsonArray definitions();

    Result execute(const QString &name, const QJsonObject &input, bool autoApproveEdits);

    // Unified diff rendered as HTML, used by the approval dialog.
    static QString diffHtml(const QString &oldText, const QString &newText);

    // Modal Accept/Reject dialog showing the given HTML.
    static bool askApproval(QWidget *parent, const QString &title, const QString &html);

    // Called before the agent changes a file, with its current state, so it can be restored later.
    std::function<void(const QString &path, bool existed, const QString &content)> beforeChange;

    // Puts a file back to a recorded state (deletes it if it did not exist).
    bool restoreFile(const QString &absolutePath, bool existed, const QString &content);

private:
    Result listFiles(const QJsonObject &input);
    Result readFile(const QJsonObject &input);
    Result search(const QJsonObject &input);
    Result editFile(const QJsonObject &input, bool autoApprove);
    Result writeFile(const QJsonObject &input, bool autoApprove);
    Result runCommand(const QJsonObject &input);

    bool resolvePath(const QString &path, QString *absolutePath, QString *error) const;
    QString relativePath(const QString &absolutePath) const;
    bool readText(const QString &absolutePath, QString *text, QString *error) const;
    Result applyChange(const QString &absolutePath, const QString &oldText, const QString &newText, bool isNew, bool autoApprove);
    QString writeContent(const QString &absolutePath, const QString &text, bool openIfClosed);

    MainWindow *window;
};
