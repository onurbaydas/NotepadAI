/*
 * This file is part of Notepad Next.
 * Copyright 2022 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QDockWidget>

namespace Ui {
class FolderAsWorkspaceDock;
}

class QAction;
class QLabel;
class QMenu;
class QModelIndex;
class QProcess;
class QStackedWidget;
class QTimer;
class WorkspaceModel;

// The Explorer: the workspace folder as a tree with file icons, git status,
// a context menu for file operations and drag and drop moves
class FolderAsWorkspaceDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit FolderAsWorkspaceDock(QWidget *parent = nullptr);
    ~FolderAsWorkspaceDock();

    void setRootPath(const QString &dir);
    QString rootPath() const;
    bool hasRoot() const;

    // View options (columns, hidden files), shown from the dock's title bar
    QMenu *optionsMenu() const;

    // Selects the file in the tree, expanding its parent folders
    void revealFile(const QString &filePath);

    void refreshGitStatus();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void fileOpenRequested(const QString &filePath);
    void openFolderRequested();
    // A new file was created and should be opened for typing
    void fileCreated(const QString &filePath);
    void pathRenamed(const QString &oldPath, const QString &newPath);
    void pathDeleted(const QString &path);
    void addToChatRequested(const QString &path);
    void openTerminalRequested(const QString &dir);

private:
    void buildTitleBar();
    void showContextMenu(const QPoint &pos);
    QString selectedPath() const;
    QString targetDirectory() const;
    void newFile();
    void newFolder();
    void renameSelected();
    void deleteSelected();
    void copyPath(bool relative);
    void revealInFileManager();
    void updateHeader();
    void runGit(const QStringList &arguments);
    void gitFinished();

    Ui::FolderAsWorkspaceDock *ui;

    WorkspaceModel *model;
    QStackedWidget *pages;
    QLabel *folderName;
    QMenu *contextMenu;

    QAction *actionNewFile;
    QAction *actionNewFolder;
    QAction *actionRefresh;
    QAction *actionCollapseAll;
    QAction *actionRename;
    QAction *actionDelete;
    QAction *actionCopyPath;
    QAction *actionCopyRelativePath;
    QAction *actionReveal;
    QAction *actionOpenTerminal;
    QAction *actionAddToChat;

    QProcess *git = nullptr;
    QString gitTopLevel;
    QString pendingReveal;
    QTimer *gitTimer;
};
