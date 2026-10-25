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


#include "FolderAsWorkspaceDock.h"
#include "ApplicationSettings.h"
#include "DarkTheme.h"
#include "UiIcons.h"
#include "ui_FolderAsWorkspaceDock.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

ApplicationSetting<QString> rootPathSetting{"FolderAsWorkspace/RootPath"};

namespace {
    // QFileSystemModel's standard column order.
    enum FileSystemColumn
    {
        NameColumn = 0,
        SizeColumn,
        TypeColumn,
        DateColumn
    };

    const int GitStatusRole = Qt::UserRole + 100;

    // Paths are compared case-insensitively on Windows
    QString pathKey(const QString &path)
    {
        QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
        key = key.toLower();
#endif
        return key;
    }

    QColor gitColour(QChar status)
    {
        const bool dark = DarkTheme::isEnabled();
        switch (status.unicode()) {
        case 'U':
        case 'A':
        case 'R':
            return dark ? QColor(0x73, 0xc9, 0x91) : QColor(0x38, 0x7a, 0x37);
        case 'D':
        case '!':
            return dark ? QColor(0xe0, 0x6c, 0x6c) : QColor(0xad, 0x07, 0x07);
        case 'M':
            return dark ? QColor(0xe2, 0xc0, 0x8d) : QColor(0x89, 0x55, 0x03);
        default:
            return QColor();
        }
    }

    // Changes in a folder colour the folder; conflicts win over edits, edits over new files
    int precedence(QChar status)
    {
        switch (status.unicode()) {
        case '!': return 3;
        case 'M': case 'D': return 2;
        case 'U': case 'A': case 'R': return 1;
        default: return 0;
        }
    }
}

class WorkspaceModel : public QFileSystemModel
{
public:
    using QFileSystemModel::QFileSystemModel;

    std::function<bool(const QStringList &, const QString &)> confirmMove;
    std::function<void(const QString &, const QString &)> moved;

    void setGitStatus(const QString &topLevel, const QByteArray &porcelain)
    {
        files.clear();
        folders.clear();
        untrackedDirs.clear();

        const QList<QByteArray> parts = porcelain.split('\0');
        for (int i = 0; i < parts.size(); ++i) {
            const QByteArray &entry = parts.at(i);
            if (entry.size() < 4)
                continue;

            const QByteArray xy = entry.left(2);
            const QString relative = QString::fromUtf8(entry.mid(3));
            if (xy[0] == 'R' || xy[0] == 'C')
                ++i; // the original path follows a rename

            QChar status = 'M';
            if (xy == "??")
                status = 'U';
            else if (xy.contains('U') || xy == "AA" || xy == "DD")
                status = '!';
            else if (xy[0] == 'A')
                status = 'A';
            else if (xy.contains('D'))
                status = 'D';
            else if (xy[0] == 'R')
                status = 'R';

            QString path = pathKey(topLevel + '/' + relative);
            if (relative.endsWith('/'))
                untrackedDirs.append(path);
            else
                files.insert(path, status);

            const QChar folderStatus = (status == 'A' || status == 'R') ? QChar('U') : (status == 'D' ? QChar('M') : status);
            for (QString parent = QFileInfo(path).path(); parent.size() >= topLevel.size() && parent.size() > 1; parent = QFileInfo(parent).path()) {
                if (precedence(folderStatus) > precedence(folders.value(parent)))
                    folders.insert(parent, folderStatus);
                if (parent == QFileInfo(parent).path())
                    break;
            }
        }
    }

    void clearGitStatus()
    {
        files.clear();
        folders.clear();
        untrackedDirs.clear();
    }

    QChar statusFor(const QModelIndex &index) const
    {
        const QString key = pathKey(filePath(index));
        if (files.contains(key))
            return files.value(key);
        for (const QString &dir : untrackedDirs) {
            if (key == dir || key.startsWith(dir + '/'))
                return 'U';
        }
        if (isDir(index) && folders.contains(key))
            return folders.value(key);
        return QChar();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (index.column() == NameColumn) {
            if (role == Qt::DecorationRole) {
                if (isDir(index))
                    return UiIcons::folderIcon();
                return UiIcons::fileIcon(fileName(index));
            }
            if (role == Qt::ForegroundRole) {
                const QColor colour = gitColour(statusFor(index));
                if (colour.isValid())
                    return colour;
            }
            if (role == GitStatusRole) {
                const QChar status = statusFor(index);
                if (status.isNull())
                    return QVariant();
                // Folders only get a dot, files the letter
                return isDir(index) ? QStringLiteral("•") : QString(status);
            }
        }
        return QFileSystemModel::data(index, role);
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override
    {
        const QString destination = filePath(parent);
        QList<QPair<QString, QString>> moves;
        QStringList names;
        for (const QUrl &url : data->urls()) {
            const QString source = url.toLocalFile();
            if (source.isEmpty() || QFileInfo(source).path() == destination)
                continue;
            moves.append({source, destination + '/' + QFileInfo(source).fileName()});
            names.append(QFileInfo(source).fileName());
        }

        if (moves.isEmpty())
            return false;
        if (action == Qt::MoveAction && confirmMove && !confirmMove(names, destination))
            return false;

        const bool ok = QFileSystemModel::dropMimeData(data, action, row, column, parent);
        if (action == Qt::MoveAction && moved) {
            for (const auto &move : moves) {
                if (!QFileInfo::exists(move.first) && QFileInfo::exists(move.second))
                    moved(move.first, move.second);
            }
        }
        return ok;
    }

private:
    QHash<QString, QChar> files;
    QHash<QString, QChar> folders;
    QStringList untrackedDirs;
};

namespace {

// Paints the git status letter at the right of a row and selects only the base name when renaming
class ExplorerDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QString status = index.data(GitStatusRole).toString();
        if (status.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        // Full-width row, with the name elided before the status letter
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const int available = option.rect.width() - opt.decorationSize.width() - 30;
        opt.text = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, qMax(available, 10));
        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        painter->save();
        painter->setPen(index.data(Qt::ForegroundRole).value<QColor>());
        QRect letter = option.rect;
        letter.setLeft(option.rect.right() - 18);
        letter.setRight(option.rect.right() - 6);
        painter->drawText(letter, Qt::AlignVCenter | Qt::AlignRight, status);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 22));
        return size;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        QStyledItemDelegate::setEditorData(editor, index);
        if (QLineEdit *line = qobject_cast<QLineEdit *>(editor)) {
            // The view selects everything after this; select the name without the extension afterwards
            QTimer::singleShot(0, line, [line]() {
                const QString name = line->text();
                const int dot = name.lastIndexOf('.');
                if (dot > 0)
                    line->setSelection(0, dot);
            });
        }
    }
};

}

FolderAsWorkspaceDock::FolderAsWorkspaceDock(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::FolderAsWorkspaceDock),
    model(new WorkspaceModel(this))
{
    ui->setupUi(this);
    setWindowTitle(tr("Explorer"));

    model->setReadOnly(false);
    model->confirmMove = [this](const QStringList &names, const QString &destination) {
        const QString what = names.size() == 1 ? names.first() : tr("%1 items").arg(names.size());
        return QMessageBox::question(this, tr("Move"),
                                     tr("Move '%1' into '%2'?").arg(what, QFileInfo(destination).fileName()),
                                     QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes) == QMessageBox::Yes;
    };
    model->moved = [this](const QString &from, const QString &to) {
        emit pathRenamed(from, to);
        refreshGitStatus();
    };
    connect(model, &QFileSystemModel::fileRenamed, this, [this](const QString &dir, const QString &oldName, const QString &newName) {
        emit pathRenamed(dir + '/' + oldName, dir + '/' + newName);
        refreshGitStatus();
    });

    ui->treeView->setModel(model);
    connect(model, &QFileSystemModel::directoryLoaded, this, [this]() {
        if (!pendingReveal.isEmpty())
            revealFile(pendingReveal);
    });
    ui->treeView->setHeaderHidden(true);
    ui->treeView->setObjectName("workspaceTree");
    ui->treeView->setItemDelegate(new ExplorerDelegate(ui->treeView));
    ui->treeView->setExpandsOnDoubleClick(false);
    ui->treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->treeView->setDragDropMode(QAbstractItemView::DragDrop);
    ui->treeView->setDefaultDropAction(Qt::MoveAction);
    ui->treeView->setDragEnabled(true);
    ui->treeView->setAcceptDrops(true);
    ui->treeView->setDropIndicatorShown(true);
    ui->treeView->setIndentation(12);
    ui->treeView->setIconSize(QSize(16, 16));
    ui->treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->treeView->setMouseTracking(true);
    ui->treeView->installEventFilter(this);

    QMenu *menu = new QMenu(ui->toolButton);
    menu->addAction(ui->actionShowSize);
    menu->addAction(ui->actionShowType);
    menu->addAction(ui->actionShowDateModified);
    menu->addSeparator();
    menu->addAction(ui->actionShowHidden);

    ui->toolButton->setMenu(menu);
    ui->toolButton->setPopupMode(QToolButton::MenuButtonPopup);
    ui->toolButton->hide();

    auto bindColumnVisibility = [this](QAction* action, int column) {
        ui->treeView->setColumnHidden(column, !action->isChecked());
        connect(action, &QAction::toggled, this, [this, column](bool checked) {
            ui->treeView->setColumnHidden(column, !checked);
        });
    };

    auto bindActionToSetting = [](QAction* action, const QString& key) {
        ApplicationSettings settings;

        action->setChecked(settings.value(key, false).toBool());
        connect(action, &QAction::toggled, [key](bool checked) {
            ApplicationSettings settings;
            settings.setValue(key, checked);
        });
    };

    auto bindColumn = [&](QAction* action, int column, const QString& key) {
        bindColumnVisibility(action, column);
        bindActionToSetting(action, key);
    };

    bindColumn(ui->actionShowSize, SizeColumn, "FolderAsWorkspace/ShowSize");
    bindColumn(ui->actionShowType, TypeColumn, "FolderAsWorkspace/ShowType");
    bindColumn(ui->actionShowDateModified, DateColumn, "FolderAsWorkspace/ShowDateModified");

    connect(ui->actionShowHidden, &QAction::toggled, this, [this](bool showHidden) {
        auto filters = QDir::AllEntries | QDir::AllDirs | QDir::NoDotAndDotDot;
        if (showHidden) filters |= QDir::Hidden;
        model->setFilter(filters);
    });
    bindActionToSetting(ui->actionShowHidden, "FolderAsWorkspace/ShowHidden");

    // Default sorting
    ui->treeView->sortByColumn(0, Qt::AscendingOrder);

    // A single click opens a file or toggles a folder, like in VS Code
    connect(ui->treeView, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (model->isDir(index)) {
            // A click on the expand arrow has already toggled the folder
            const QPoint pos = ui->treeView->viewport()->mapFromGlobal(QCursor::pos());
            if (pos.x() >= ui->treeView->visualRect(index).left())
                ui->treeView->setExpanded(index, !ui->treeView->isExpanded(index));
            return;
        }
        emit fileOpenRequested(model->filePath(index));
    });

    connect(ui->treeView, &QTreeView::customContextMenuRequested, this, &FolderAsWorkspaceDock::showContextMenu);

    // Configure header settings
    QHeaderView *header = ui->treeView->header();
    header->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(SizeColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(TypeColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(DateColumn, QHeaderView::ResizeToContents);

    // Actions
    actionNewFile = new QAction(UiIcons::glyph(UiIcons::NewFile), tr("New File..."), this);
    actionNewFolder = new QAction(UiIcons::glyph(UiIcons::NewFolder), tr("New Folder..."), this);
    actionRefresh = new QAction(UiIcons::glyph(UiIcons::Refresh), tr("Refresh Explorer"), this);
    actionCollapseAll = new QAction(UiIcons::glyph(UiIcons::CollapseAll), tr("Collapse Folders"), this);
    actionRename = new QAction(tr("Rename..."), this);
    actionRename->setShortcut(QKeySequence(Qt::Key_F2));
    actionDelete = new QAction(UiIcons::glyph(UiIcons::Trash), tr("Delete"), this);
    actionDelete->setShortcut(QKeySequence(Qt::Key_Delete));
    actionCopyPath = new QAction(tr("Copy Path"), this);
    actionCopyRelativePath = new QAction(tr("Copy Relative Path"), this);
#ifdef Q_OS_WIN
    actionReveal = new QAction(tr("Reveal in File Explorer"), this);
#else
    actionReveal = new QAction(tr("Open Containing Folder"), this);
#endif
    actionOpenTerminal = new QAction(UiIcons::glyph(UiIcons::Terminal), tr("Open in Terminal"), this);
    actionAddToChat = new QAction(tr("Add to AI Chat"), this);

    connect(actionNewFile, &QAction::triggered, this, &FolderAsWorkspaceDock::newFile);
    connect(actionNewFolder, &QAction::triggered, this, &FolderAsWorkspaceDock::newFolder);
    connect(actionRefresh, &QAction::triggered, this, [this]() {
        // Re-reading the root makes QFileSystemModel pick up changes its watcher missed
        const QString root = rootPath();
        model->setRootPath(QString());
        ui->treeView->setRootIndex(model->setRootPath(root));
        refreshGitStatus();
    });
    connect(actionCollapseAll, &QAction::triggered, ui->treeView, &QTreeView::collapseAll);
    connect(actionRename, &QAction::triggered, this, &FolderAsWorkspaceDock::renameSelected);
    connect(actionDelete, &QAction::triggered, this, &FolderAsWorkspaceDock::deleteSelected);
    connect(actionCopyPath, &QAction::triggered, this, [this]() { copyPath(false); });
    connect(actionCopyRelativePath, &QAction::triggered, this, [this]() { copyPath(true); });
    connect(actionReveal, &QAction::triggered, this, &FolderAsWorkspaceDock::revealInFileManager);
    connect(actionOpenTerminal, &QAction::triggered, this, [this]() { emit openTerminalRequested(targetDirectory()); });
    connect(actionAddToChat, &QAction::triggered, this, [this]() {
        const QString path = selectedPath();
        if (!path.isEmpty())
            emit addToChatRequested(path);
    });

    contextMenu = new QMenu(this);

    // Empty state when no folder is open
    QWidget *empty = new QWidget;
    empty->setObjectName("explorerEmpty");
    QVBoxLayout *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(16, 16, 16, 16);
    emptyLayout->setSpacing(12);
    QLabel *emptyText = new QLabel(tr("You have not opened a folder yet. Open your project folder to browse its files and let the AI work in it."), empty);
    emptyText->setWordWrap(true);
    emptyText->setObjectName("dockStatus");
    QPushButton *openButton = new QPushButton(tr("Open Folder"), empty);
    openButton->setObjectName("primaryButton");
    openButton->setCursor(Qt::PointingHandCursor);
    connect(openButton, &QPushButton::clicked, this, &FolderAsWorkspaceDock::openFolderRequested);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addWidget(openButton);
    emptyLayout->addStretch();

    // Folder name above the tree
    QWidget *treePage = new QWidget;
    QVBoxLayout *treeLayout = new QVBoxLayout(treePage);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(0);
    folderName = new QLabel(treePage);
    folderName->setObjectName("workspaceHeader");
    treeLayout->addWidget(folderName);
    ui->verticalLayout->removeWidget(ui->treeView);
    treeLayout->addWidget(ui->treeView);

    pages = new QStackedWidget(ui->dockWidgetContents);
    pages->addWidget(empty);
    pages->addWidget(treePage);
    ui->verticalLayout->insertWidget(0, pages);

    buildTitleBar();

    gitTimer = new QTimer(this);
    gitTimer->setInterval(5000);
    connect(gitTimer, &QTimer::timeout, this, [this]() {
        if (isVisible() && QGuiApplication::applicationState() == Qt::ApplicationActive)
            refreshGitStatus();
    });
    gitTimer->start();

    ApplicationSettings settings;
    setRootPath(settings.get(rootPathSetting));
}

FolderAsWorkspaceDock::~FolderAsWorkspaceDock()
{
    delete ui;
}

bool FolderAsWorkspaceDock::eventFilter(QObject *obj, QEvent *event)
{
    if (obj != ui->treeView)
        return QDockWidget::eventFilter(obj, event);

    // Keys the tree handles itself, even when the main window has a shortcut for them
    auto handled = [](int key) {
        return key == Qt::Key_F2 || key == Qt::Key_Delete || key == Qt::Key_Return || key == Qt::Key_Enter;
    };

    if (event->type() == QEvent::ShortcutOverride) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->modifiers() == Qt::NoModifier && handled(keyEvent->key())) {
            event->accept();
            return true;
        }
    }
    else if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->modifiers() != Qt::NoModifier || !handled(keyEvent->key()))
            return QDockWidget::eventFilter(obj, event);

        switch (keyEvent->key()) {
        case Qt::Key_F2:
            renameSelected();
            return true;
        case Qt::Key_Delete:
            deleteSelected();
            return true;
        default: {
            const QModelIndex index = ui->treeView->currentIndex();
            if (!index.isValid())
                return true;
            if (model->isDir(index))
                ui->treeView->setExpanded(index, !ui->treeView->isExpanded(index));
            else
                emit fileOpenRequested(model->filePath(index));
            return true;
        }
        }
    }

    return QDockWidget::eventFilter(obj, event);
}

void FolderAsWorkspaceDock::buildTitleBar()
{
    QWidget *bar = new QWidget(this);
    bar->setObjectName("dockTitleBar");
    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(10, 3, 4, 3);
    layout->setSpacing(0);

    QLabel *title = new QLabel(tr("EXPLORER"), bar);
    title->setObjectName("dockTitle");
    layout->addWidget(title);
    layout->addStretch();

    auto addButton = [bar, layout](QAction *action) {
        QToolButton *button = new QToolButton(bar);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        layout->addWidget(button);
        return button;
    };
    addButton(actionNewFile);
    addButton(actionNewFolder);
    addButton(actionRefresh);
    addButton(actionCollapseAll);

    QMenu *more = new QMenu(bar);
    QAction *openFolder = more->addAction(UiIcons::glyph(UiIcons::FolderOpen), tr("Open Folder..."));
    connect(openFolder, &QAction::triggered, this, &FolderAsWorkspaceDock::openFolderRequested);
    more->addAction(actionOpenTerminal);
    more->addSeparator();
    QMenu *columns = more->addMenu(tr("Show"));
    columns->addActions(optionsMenu()->actions());

    QToolButton *moreButton = new QToolButton(bar);
    moreButton->setIcon(UiIcons::glyph(UiIcons::More));
    moreButton->setToolTip(tr("More actions"));
    moreButton->setAutoRaise(true);
    moreButton->setPopupMode(QToolButton::InstantPopup);
    moreButton->setMenu(more);
    moreButton->setStyleSheet("QToolButton::menu-indicator { image: none; }");
    layout->addWidget(moreButton);

    QToolButton *close = new QToolButton(bar);
    close->setIcon(UiIcons::glyph(UiIcons::Close));
    close->setToolTip(tr("Close"));
    close->setAutoRaise(true);
    connect(close, &QToolButton::clicked, this, &QDockWidget::close);
    layout->addWidget(close);

    setTitleBarWidget(bar);
}

void FolderAsWorkspaceDock::setRootPath(const QString &dir)
{
    ApplicationSettings settings;
    settings.set(rootPathSetting, dir);

    const QModelIndex sourceIndex = model->setRootPath(dir);
    ui->treeView->setRootIndex(sourceIndex);

    gitTopLevel.clear();
    model->clearGitStatus();
    updateHeader();
    refreshGitStatus();
}

QMenu *FolderAsWorkspaceDock::optionsMenu() const
{
    return ui->toolButton->menu();
}

QString FolderAsWorkspaceDock::rootPath() const
{
    return model->rootPath();
}

bool FolderAsWorkspaceDock::hasRoot() const
{
    const QString root = rootPath();
    return !root.isEmpty() && QFileInfo(root).isDir() && !QDir(root).isRoot();
}

void FolderAsWorkspaceDock::updateHeader()
{
    const bool open = hasRoot();
    pages->setCurrentIndex(open ? 1 : 0);
    actionNewFile->setEnabled(open);
    actionNewFolder->setEnabled(open);
    actionRefresh->setEnabled(open);
    actionCollapseAll->setEnabled(open);

    folderName->setText(QFileInfo(rootPath()).fileName().toUpper());
    folderName->setToolTip(QDir::toNativeSeparators(rootPath()));
}

void FolderAsWorkspaceDock::revealFile(const QString &filePath)
{
    if (!hasRoot() || filePath.isEmpty())
        return;

    const QString root = pathKey(rootPath());
    if (!pathKey(filePath).startsWith(root + '/'))
        return;

    // Folders load asynchronously; try again as they come in
    if (pendingReveal != filePath) {
        pendingReveal = filePath;
        QTimer::singleShot(3000, this, [this, filePath]() {
            if (pendingReveal == filePath)
                pendingReveal.clear();
        });
    }

    const QModelIndex index = model->index(filePath);
    if (!index.isValid())
        return;

    for (QModelIndex parent = index.parent(); parent.isValid() && parent != ui->treeView->rootIndex(); parent = parent.parent())
        ui->treeView->expand(parent);

    ui->treeView->setCurrentIndex(index);
    ui->treeView->scrollTo(index);
    pendingReveal.clear();
}

QString FolderAsWorkspaceDock::selectedPath() const
{
    const QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid() || !ui->treeView->selectionModel()->isSelected(index))
        return QString();
    return model->filePath(index);
}

QString FolderAsWorkspaceDock::targetDirectory() const
{
    const QString path = selectedPath();
    if (path.isEmpty())
        return rootPath();
    return QFileInfo(path).isDir() ? path : QFileInfo(path).path();
}

void FolderAsWorkspaceDock::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = ui->treeView->indexAt(pos);
    if (index.isValid())
        ui->treeView->setCurrentIndex(index);
    else
        ui->treeView->clearSelection();

    const bool hasItem = index.isValid();

    contextMenu->clear();
    contextMenu->addAction(actionNewFile);
    contextMenu->addAction(actionNewFolder);
    contextMenu->addSeparator();
    if (hasItem) {
        contextMenu->addAction(actionAddToChat);
        contextMenu->addAction(actionOpenTerminal);
        contextMenu->addAction(actionReveal);
        contextMenu->addSeparator();
        contextMenu->addAction(actionCopyPath);
        contextMenu->addAction(actionCopyRelativePath);
        contextMenu->addSeparator();
        contextMenu->addAction(actionRename);
        contextMenu->addAction(actionDelete);
    }
    else {
        contextMenu->addAction(actionOpenTerminal);
        contextMenu->addAction(actionRefresh);
        contextMenu->addAction(actionCollapseAll);
    }

    contextMenu->popup(ui->treeView->viewport()->mapToGlobal(pos));
}

static bool isValidName(const QString &name)
{
    if (name.trimmed().isEmpty())
        return false;
    const QStringList parts = QDir::fromNativeSeparators(name).split('/', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String("..") || part == QLatin1String("."))
            return false;
    }
    return !parts.isEmpty();
}

void FolderAsWorkspaceDock::newFile()
{
    if (!hasRoot())
        return;

    const QString dir = targetDirectory();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New File"),
                                               tr("File name (use / to create it inside new folders):"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (!isValidName(name)) {
        QMessageBox::warning(this, tr("New File"), tr("'%1' is not a valid file name.").arg(name));
        return;
    }

    const QString path = QDir::cleanPath(dir + '/' + QDir::fromNativeSeparators(name));
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("New File"), tr("'%1' already exists.").arg(name));
        return;
    }

    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("New File"), tr("Could not create '%1': %2").arg(name, file.errorString()));
        return;
    }
    file.close();

    emit fileCreated(path);
    // The model lists new files asynchronously
    QTimer::singleShot(300, this, [this, path]() { revealFile(path); });
}

void FolderAsWorkspaceDock::newFolder()
{
    if (!hasRoot())
        return;

    const QString dir = targetDirectory();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (!isValidName(name)) {
        QMessageBox::warning(this, tr("New Folder"), tr("'%1' is not a valid folder name.").arg(name));
        return;
    }

    const QString path = QDir::cleanPath(dir + '/' + QDir::fromNativeSeparators(name));
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("New Folder"), tr("'%1' already exists.").arg(name));
        return;
    }
    if (!QDir().mkpath(path)) {
        QMessageBox::warning(this, tr("New Folder"), tr("Could not create '%1'.").arg(name));
        return;
    }

    QTimer::singleShot(300, this, [this, path]() {
        revealFile(path);
        ui->treeView->expand(model->index(path));
    });
}

void FolderAsWorkspaceDock::renameSelected()
{
    const QModelIndex index = ui->treeView->currentIndex();
    if (index.isValid() && !selectedPath().isEmpty())
        ui->treeView->edit(index.siblingAtColumn(NameColumn));
}

void FolderAsWorkspaceDock::deleteSelected()
{
    const QString path = selectedPath();
    if (path.isEmpty() || pathKey(path) == pathKey(rootPath()))
        return;

    const QString name = QFileInfo(path).fileName();
    QMessageBox box(QMessageBox::Question, tr("Delete"),
                    tr("Are you sure you want to delete '%1'?").arg(name),
                    QMessageBox::NoButton, this);
    box.setInformativeText(QFileInfo(path).isDir() ? tr("The folder and everything in it will be moved to the trash.")
                                                   : tr("You can restore it from the trash."));
    QPushButton *trash = box.addButton(tr("Move to Trash"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(trash);
    box.exec();
    if (box.clickedButton() != trash)
        return;

    if (!QFile::moveToTrash(path)) {
        if (QMessageBox::warning(this, tr("Delete"),
                                 tr("'%1' could not be moved to the trash. Delete it permanently?").arg(name),
                                 QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;

        const bool removed = QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (!removed) {
            QMessageBox::warning(this, tr("Delete"), tr("Could not delete '%1'.").arg(name));
            return;
        }
    }

    emit pathDeleted(path);
    refreshGitStatus();
}

void FolderAsWorkspaceDock::copyPath(bool relative)
{
    const QString path = selectedPath();
    if (path.isEmpty())
        return;

    const QString text = relative ? QDir(rootPath()).relativeFilePath(path) : path;
    QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(text));
}

void FolderAsWorkspaceDock::revealInFileManager()
{
    const QString path = selectedPath();
    if (path.isEmpty())
        return;

#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).isDir() ? path : QFileInfo(path).path()));
#endif
}

void FolderAsWorkspaceDock::refreshGitStatus()
{
    if (!hasRoot() || (git && git->state() != QProcess::NotRunning))
        return;

    if (gitTopLevel.isEmpty())
        runGit({"rev-parse", "--show-toplevel"});
    else
        runGit({"status", "--porcelain=v1", "-z"});
}

void FolderAsWorkspaceDock::runGit(const QStringList &arguments)
{
    if (!git) {
        git = new QProcess(this);
        connect(git, &QProcess::finished, this, &FolderAsWorkspaceDock::gitFinished);
        connect(git, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            // git is not installed: stop asking
            if (error == QProcess::FailedToStart)
                gitTimer->stop();
        });
    }

    git->setWorkingDirectory(rootPath());
    git->setProperty("command", arguments.first());
    git->start("git", arguments);
}

void FolderAsWorkspaceDock::gitFinished()
{
    const QByteArray output = git->readAllStandardOutput();
    const bool ok = git->exitStatus() == QProcess::NormalExit && git->exitCode() == 0;
    const QString command = git->property("command").toString();

    if (command == QLatin1String("rev-parse")) {
        if (!ok) {
            // Not a git repository
            model->clearGitStatus();
            ui->treeView->viewport()->update();
            return;
        }
        gitTopLevel = pathKey(QString::fromUtf8(output).trimmed());
        QTimer::singleShot(0, this, [this]() { runGit({"status", "--porcelain=v1", "-z"}); });
        return;
    }

    if (ok)
        model->setGitStatus(gitTopLevel, output);
    else
        model->clearGitStatus();
    ui->treeView->viewport()->update();
}
