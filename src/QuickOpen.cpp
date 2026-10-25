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

#include "QuickOpen.h"

#include "FolderAsWorkspaceDock.h"
#include "MainWindow.h"
#include "ScintillaNext.h"
#include "UiIcons.h"

#include <QAction>
#include <algorithm>
#include <functional>
#include <QDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

namespace {

enum Roles {
    PathRole = Qt::UserRole,
    DetailRole,
    ShortcutRole,
    ActionRole,
    LineRole,
};

const int MAX_FILES = 30000;
const int MAX_RESULTS = 60;

const QSet<QString> SKIPPED_DIRS = {
    ".git", ".hg", ".svn", "node_modules", "__pycache__", ".venv", "venv", "env",
    ".idea", ".vs", ".vscode", "build", "dist", "out", "target", "bin", "obj",
    ".next", ".cache", ".pytest_cache", ".mypy_cache", ".gradle",
};

// Name, then a dimmer folder or menu path, and the shortcut at the right
class ResultDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString name = opt.text;
        opt.text.clear();
        opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const QRect textRect = opt.widget->style()->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget).adjusted(4, 0, -8, 0);
        const bool selected = opt.state & QStyle::State_Selected;
        const QColor text = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        QColor dim = text;
        dim.setAlpha(140);

        painter->save();
        QRect remaining = textRect;

        const QString shortcut = index.data(ShortcutRole).toString();
        if (!shortcut.isEmpty()) {
            painter->setPen(dim);
            const int width = opt.fontMetrics.horizontalAdvance(shortcut);
            painter->drawText(remaining, Qt::AlignVCenter | Qt::AlignRight, shortcut);
            remaining.setRight(remaining.right() - width - 16);
        }

        painter->setPen(text);
        const QString elidedName = opt.fontMetrics.elidedText(name, Qt::ElideMiddle, remaining.width());
        painter->drawText(remaining, Qt::AlignVCenter | Qt::AlignLeft, elidedName);

        const QString detail = index.data(DetailRole).toString();
        const int nameWidth = opt.fontMetrics.horizontalAdvance(elidedName) + 10;
        if (!detail.isEmpty() && nameWidth < remaining.width()) {
            QRect detailRect = remaining.adjusted(nameWidth, 0, 0, 0);
            painter->setPen(dim);
            painter->drawText(detailRect, Qt::AlignVCenter | Qt::AlignLeft, opt.fontMetrics.elidedText(detail, Qt::ElideLeft, detailRect.width()));
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 24));
        return size;
    }
};

QString cleanActionText(QString text)
{
    text.remove('&');
    if (text.endsWith(QLatin1String("...")))
        text.chop(3);
    return text.trimmed();
}

}

QuickOpen::QuickOpen(MainWindow *window) :
    QFrame(window, Qt::Popup),
    window(window)
{
    setObjectName("quickOpen");
    setFrameShape(QFrame::StyledPanel);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    input = new QLineEdit(this);
    input->setObjectName("quickOpenInput");
    input->setClearButtonEnabled(false);
    input->installEventFilter(this);
    connect(input, &QLineEdit::textChanged, this, &QuickOpen::updateResults);

    list = new QListWidget(this);
    list->setObjectName("quickOpenList");
    list->setFrameShape(QFrame::NoFrame);
    list->setItemDelegate(new ResultDelegate(list));
    list->setUniformItemSizes(true);
    list->setFocusPolicy(Qt::NoFocus);
    list->setIconSize(QSize(16, 16));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(list, &QListWidget::itemClicked, this, [this]() { acceptCurrent(); });

    layout->addWidget(input);
    layout->addWidget(list);
}

void QuickOpen::showFiles()
{
    collectFiles();
    input->setPlaceholderText(tr("Search files by name (append :line to go to a line, start with > for commands)"));
    popup(QString());
}

void QuickOpen::showCommands()
{
    input->setPlaceholderText(tr("Type a command"));
    popup(QStringLiteral(">"));
}

void QuickOpen::popup(const QString &text)
{
    const QRect area = window->centralWidget() ? window->centralWidget()->geometry() : window->rect();
    const int width = qBound(420, window->width() * 55 / 100, 680);
    const QPoint topCenter = window->mapToGlobal(QPoint(window->width() / 2, area.top() + 4));
    setFixedWidth(width);
    move(topCenter.x() - width / 2, topCenter.y());

    input->setText(text);
    updateResults();
    show();
    raise();
    input->setFocus();
    input->deselect();
    input->setCursorPosition(input->text().size());
}

void QuickOpen::collectFiles()
{
    files.clear();

    FolderAsWorkspaceDock *workspace = window->findChild<FolderAsWorkspaceDock *>();
    root = workspace && workspace->hasRoot() ? workspace->rootPath() : QString();
    if (root.isEmpty())
        return;

    // Breadth first so the files near the top of the project come first if the limit is reached
    QStringList queue{root};
    while (!queue.isEmpty() && files.size() < MAX_FILES) {
        const QString dir = queue.takeFirst();
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
        for (const QFileInfo &entry : entries) {
            if (entry.isDir()) {
                if (!SKIPPED_DIRS.contains(entry.fileName()) && !entry.isSymLink())
                    queue.append(entry.absoluteFilePath());
            }
            else {
                files.append(entry.absoluteFilePath());
            }
        }
    }
}

void QuickOpen::collectCommands()
{
    commands.clear();

    std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &prefix) {
        for (QAction *action : menu->actions()) {
            if (action->isSeparator() || !action->isVisible())
                continue;
            const QString text = cleanActionText(action->text());
            if (text.isEmpty())
                continue;
            if (action->menu()) {
                walk(action->menu(), prefix + text + QStringLiteral(" › "));
                continue;
            }
            commands.append({prefix + text, action});
        }
    };

    for (QAction *action : window->menuBar()->actions()) {
        if (action->menu())
            walk(action->menu(), cleanActionText(action->text()) + QStringLiteral(" › "));
    }
}

int QuickOpen::fuzzyScore(const QString &query, const QString &text)
{
    if (query.isEmpty())
        return 0;

    int score = 0;
    int qi = 0;
    int previousMatch = -2;
    for (int i = 0; i < text.size() && qi < query.size(); ++i) {
        if (text.at(i).toLower() != query.at(qi))
            continue;

        int bonus = 1;
        if (i == previousMatch + 1)
            bonus += 5;
        if (i == 0)
            bonus += 8;
        else {
            const QChar before = text.at(i - 1);
            if (before == '/' || before == '\\' || before == '_' || before == '-' || before == '.' || before == ' ')
                bonus += 6;
            else if (text.at(i).isUpper() && before.isLower())
                bonus += 6;
        }
        score += bonus;
        previousMatch = i;
        ++qi;
    }

    if (qi < query.size())
        return -1;

    // Exact substrings rank above scattered matches
    if (text.contains(query, Qt::CaseInsensitive))
        score += 40;

    // Shorter texts are better matches
    return score * 10 - qMin(text.size(), 100) / 4;
}

void QuickOpen::updateResults()
{
    list->clear();

    const QString text = input->text();

    if (text.startsWith('>')) {
        if (commands.isEmpty())
            collectCommands();

        const QString query = text.mid(1).trimmed().toLower().remove(' ');
        QList<QPair<int, int>> scored;
        for (int i = 0; i < commands.size(); ++i) {
            const QAction *action = commands.at(i).second;
            if (!action || !action->isEnabled())
                continue;
            const int score = fuzzyScore(query, commands.at(i).first);
            if (score >= 0)
                scored.append({score, i});
        }
        if (!query.isEmpty())
            std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

        for (const auto &entry : scored.mid(0, query.isEmpty() ? 400 : MAX_RESULTS)) {
            const auto &command = commands.at(entry.second);
            const int split = command.first.lastIndexOf(QStringLiteral(" › "));
            QListWidgetItem *item = new QListWidgetItem(list);
            item->setText(split >= 0 ? command.first.mid(split + 3) : command.first);
            item->setData(DetailRole, split >= 0 ? command.first.left(split) : QString());
            item->setData(ShortcutRole, command.second->shortcut().toString(QKeySequence::NativeText));
            item->setData(ActionRole, QVariant::fromValue<QObject *>(command.second.data()));
            if (!command.second->icon().isNull())
                item->setIcon(command.second->icon());
        }
    }
    else {
        commands.clear();

        // "name:42" opens a file at a line, ":42" goes to a line in the current file
        QString query = text.trimmed();
        int line = -1;
        static const QRegularExpression linePattern(QStringLiteral("^(.*):(\\d+)$"));
        const QRegularExpressionMatch match = linePattern.match(query);
        if (match.hasMatch()) {
            query = match.captured(1).trimmed();
            line = match.captured(2).toInt();
        }

        if (query.isEmpty() && line > 0) {
            QListWidgetItem *item = new QListWidgetItem(list);
            item->setText(tr("Go to line %1").arg(line));
            item->setData(LineRole, line);
        }

        query = query.toLower().remove(' ');

        // Open files first, then the workspace files
        QList<QPair<int, QString>> scored;
        QSet<QString> seen;
        int openRank = 0;
        for (ScintillaNext *editor : window->editors()) {
            if (!editor->isFile())
                continue;
            const QString path = editor->getFilePath();
            seen.insert(path);
            const QString shown = !root.isEmpty() && path.startsWith(root + '/') ? path.mid(root.size() + 1) : QFileInfo(path).fileName();
            const int nameScore = fuzzyScore(query, editor->getName());
            const int score = query.isEmpty() ? 1000000 - openRank++ : qMax(nameScore >= 0 ? nameScore + 200 : -1, fuzzyScore(query, shown));
            if (score >= 0)
                scored.append({score + 50, path});
        }

        for (const QString &path : std::as_const(files)) {
            if (seen.contains(path))
                continue;
            const QString relative = path.mid(root.size() + 1);
            const int nameScore = fuzzyScore(query, QFileInfo(path).fileName());
            const int score = query.isEmpty() ? 0 : qMax(nameScore >= 0 ? nameScore + 200 : -1, fuzzyScore(query, relative));
            if (score >= 0)
                scored.append({score, path});
        }

        std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

        for (const auto &entry : scored.mid(0, MAX_RESULTS)) {
            const QString &path = entry.second;
            QListWidgetItem *item = new QListWidgetItem(list);
            item->setText(QFileInfo(path).fileName());
            QString detail = !root.isEmpty() && path.startsWith(root + '/') ? QFileInfo(path.mid(root.size() + 1)).path() : QFileInfo(path).path();
            if (detail == QLatin1String("."))
                detail.clear();
            item->setData(DetailRole, QDir::toNativeSeparators(detail));
            item->setData(PathRole, path);
            item->setData(LineRole, line);
            item->setIcon(UiIcons::fileIcon(item->text()));
        }

        if (list->count() == 0 && !query.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem(list);
            item->setText(root.isEmpty() ? tr("No matching open files. Open a folder to search its files.") : tr("No matching files"));
            item->setFlags(Qt::NoItemFlags);
        }
    }

    if (list->count() > 0 && (list->item(0)->flags() & Qt::ItemIsEnabled))
        list->setCurrentRow(0);

    // Fit the popup to the results
    const int rows = qBound(1, list->count(), 14);
    list->setFixedHeight(rows * 24 + 4);
    adjustSize();
}

void QuickOpen::acceptCurrent()
{
    QListWidgetItem *item = list->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;

    const QString path = item->data(PathRole).toString();
    const int line = item->data(LineRole).toInt();
    QPointer<QAction> action = qobject_cast<QAction *>(item->data(ActionRole).value<QObject *>());

    hide();

    // Run after the popup has closed so dialogs and focus behave normally
    QTimer::singleShot(0, window, [this, path, line, action]() {
        if (action) {
            action->trigger();
            return;
        }

        if (!path.isEmpty())
            window->openFile(path);

        ScintillaNext *editor = window->currentEditor();
        if (editor && line > 0) {
            const int target = qMin(line, (int) editor->lineCount()) - 1;
            editor->ensureVisibleEnforcePolicy(target);
            editor->gotoLine(target);
            editor->verticalCentreCaret();
        }
        if (editor)
            editor->QWidget::setFocus();
    });
}

bool QuickOpen::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == input && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp: {
            if (list->count() == 0)
                return true;
            const int step = (keyEvent->key() == Qt::Key_PageDown || keyEvent->key() == Qt::Key_PageUp) ? 10 : 1;
            const int direction = (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_PageDown) ? step : -step;
            int row = list->currentRow() + direction;
            if (step == 1)
                row = (row + list->count()) % list->count();
            list->setCurrentRow(qBound(0, row, list->count() - 1));
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            acceptCurrent();
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(obj, event);
}
