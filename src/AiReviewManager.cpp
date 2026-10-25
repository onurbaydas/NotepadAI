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

#include "AiReviewManager.h"

#include "DarkTheme.h"
#include "MainWindow.h"
#include "ScintillaNext.h"

#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QToolButton>

namespace {

// Marker numbers not used by Notepad Next (21-24 and the fold markers are taken)
const int MARK_AI_ADDED = 18;
const int MARK_AI_ADDED_BAR = 19;

const char *ANNOTATION_STYLES_PROPERTY = "ai_annotation_styles";

QString editorText(ScintillaNext *editor)
{
    return QString::fromUtf8(editor->getText(editor->textLength() + 1));
}

// Replaces the whole buffer as one undoable step and saves it if it is a file
void setEditorText(ScintillaNext *editor, const QString &text)
{
    const QByteArray data = text.toUtf8();
    editor->targetWholeDocument();
    editor->replaceTargetMinimal(data.size(), data.constData());
    if (editor->isFile())
        editor->save();
}

sptr_t rgb(int r, int g, int b)
{
    return r | (g << 8) | (b << 16);
}

}

AiReviewManager::AiReviewManager(MainWindow *window) :
    QObject(window),
    window(window)
{
}

int AiReviewManager::annotationStyles(ScintillaNext *editor)
{
    QVariant property = editor->QObject::property(ANNOTATION_STYLES_PROPERTY);
    if (!property.isValid()) {
        property = static_cast<int>(editor->allocateExtendedStyles(2));
        editor->QObject::setProperty(ANNOTATION_STYLES_PROPERTY, property);
    }
    const int base = property.toInt();
    const bool dark = DarkTheme::isEnabled();
    const QByteArray font = editor->styleFont(STYLE_DEFAULT);
    const sptr_t size = editor->styleSize(STYLE_DEFAULT);

    // Language changes reset styles, so set them every time
    editor->styleSetFore(base, 0x8C8C8C);
    editor->styleSetBack(base, editor->styleBack(STYLE_DEFAULT));
    editor->styleSetItalic(base, true);
    editor->styleSetFont(base, font.constData());
    editor->styleSetSize(base, size);

    editor->styleSetFore(base + 1, dark ? rgb(0xff, 0xb4, 0xb4) : rgb(0x82, 0x07, 0x1e));
    editor->styleSetBack(base + 1, dark ? rgb(0x4a, 0x21, 0x25) : rgb(0xff, 0xeb, 0xe9));
    editor->styleSetItalic(base + 1, false);
    editor->styleSetFont(base + 1, font.constData());
    editor->styleSetSize(base + 1, size);

    editor->eOLAnnotationSetStyleOffset(base);
    editor->annotationSetStyleOffset(base);
    editor->eOLAnnotationSetVisible(EOLANNOTATION_STANDARD);
    editor->annotationSetVisible(ANNOTATION_STANDARD);
    return base;
}

AiReviewManager::Review *AiReviewManager::find(ScintillaNext *editor) const
{
    for (Review *review : reviews) {
        if (review->editor == editor)
            return review;
    }
    return nullptr;
}

bool AiReviewManager::isUnderReview(ScintillaNext *editor) const
{
    return find(editor) != nullptr;
}

AiReviewManager::Review *AiReviewManager::create(ScintillaNext *editor, const QString &original)
{
    Review *review = new Review;
    review->editor = editor;
    review->original = original;
    reviews.append(review);

    editor->installEventFilter(this);
    connect(editor, &QObject::destroyed, this, [this, review]() {
        // The bar was a child of the editor and is gone too
        reviews.removeOne(review);
        delete review;
        emit changed();
    });

    createBar(review);
    return review;
}

void AiReviewManager::applyToEditor(ScintillaNext *editor, const QString &newText, bool newFile)
{
    Review *review = find(editor);
    if (!review) {
        review = create(editor, newFile ? QString() : editorText(editor));
        review->newFile = newFile;
    }
    setEditorText(editor, newText);
    refresh(review);
}

void AiReviewManager::startReview(ScintillaNext *editor, const QString &original)
{
    Review *review = find(editor);
    if (!review)
        review = create(editor, original);
    refresh(review);
}

QList<AiReviewManager::FileSummary> AiReviewManager::summaries() const
{
    QList<FileSummary> result;
    for (const Review *review : reviews) {
        if (!review->editor)
            continue;
        FileSummary summary{review->editor, review->editor->getName(), 0, 0};
        for (const AiDiff::Hunk &hunk : review->hunks) {
            summary.added += hunk.added;
            summary.removed += hunk.removed;
        }
        result.append(summary);
    }
    return result;
}

void AiReviewManager::refresh(Review *review)
{
    if (!review->editor) {
        finish(review);
        return;
    }

    review->ops = AiDiff::diff(AiDiff::lines(review->original), AiDiff::lines(editorText(review->editor)));
    review->hunks = AiDiff::hunks(review->ops);
    if (review->hunks.isEmpty()) {
        finish(review);
        return;
    }

    review->current = qBound(0, review->current, static_cast<int>(review->hunks.size()) - 1);
    decorate(review);
    review->label->setText(tr("AI change %1 of %2").arg(review->current + 1).arg(review->hunks.size()));
    review->bar->adjustSize();
    positionBar(review);
    review->bar->show();
    review->bar->raise();
    emit changed();
}

void AiReviewManager::finish(Review *review)
{
    reviews.removeOne(review);
    if (review->editor) {
        clearDecorations(review);
        review->editor->removeEventFilter(this);
        disconnect(review->editor, &QObject::destroyed, this, nullptr);
    }
    if (review->bar)
        review->bar->deleteLater();
    delete review;
    emit changed();
}

void AiReviewManager::clearDecorations(Review *review)
{
    ScintillaNext *editor = review->editor;
    editor->markerDeleteAll(MARK_AI_ADDED);
    editor->markerDeleteAll(MARK_AI_ADDED_BAR);
    for (int line : std::as_const(review->annotatedLines))
        editor->annotationSetText(line, nullptr);
    review->annotatedLines.clear();
}

void AiReviewManager::decorate(Review *review)
{
    ScintillaNext *editor = review->editor;
    clearDecorations(review);

    const bool dark = DarkTheme::isEnabled();
    editor->markerDefine(MARK_AI_ADDED, SC_MARK_BACKGROUND);
    editor->markerSetBack(MARK_AI_ADDED, dark ? rgb(0x1f, 0x3d, 0x2b) : rgb(0xdc, 0xfc, 0xe4));
    editor->markerDefine(MARK_AI_ADDED_BAR, SC_MARK_FULLRECT);
    editor->markerSetBack(MARK_AI_ADDED_BAR, rgb(0x3f, 0xb9, 0x50));
    editor->setMarginMaskN(1, editor->marginMaskN(1) | (1 << MARK_AI_ADDED_BAR));
    annotationStyles(editor);

    // Removed lines are shown under the line before the place they were removed from
    int line = 0;
    QStringList removed;
    auto flushRemoved = [&]() {
        if (removed.isEmpty())
            return;
        const int target = qMax(0, line - 1);
        QString text = QString::fromUtf8(editor->annotationText(target));
        if (!text.isEmpty())
            text += '\n';
        for (QString r : std::as_const(removed))
            text += r.remove('\r') + '\n';
        text.chop(1);
        editor->annotationSetText(target, text.toUtf8().constData());
        editor->annotationSetStyle(target, 1);  // relative to the style offset: removed lines
        if (!review->annotatedLines.contains(target))
            review->annotatedLines.append(target);
        removed.clear();
    };

    for (const AiDiff::Op &op : std::as_const(review->ops)) {
        if (op.type == '-') {
            removed.append(op.line);
            continue;
        }
        flushRemoved();
        if (op.type == '+') {
            editor->markerAdd(line, MARK_AI_ADDED);
            editor->markerAdd(line, MARK_AI_ADDED_BAR);
        }
        ++line;
    }
    flushRemoved();
}

void AiReviewManager::createBar(Review *review)
{
    ScintillaNext *editor = review->editor;
    const bool dark = DarkTheme::isEnabled();

    QFrame *bar = new QFrame(editor);
    bar->setObjectName("aiReviewBar");
    bar->setStyleSheet(QString(R"(
#aiReviewBar { background-color: %1; border: 1px solid %2; border-radius: 6px; }
#aiReviewBar QLabel { color: %3; padding: 0 4px; }
#aiReviewBar QToolButton { border: none; padding: 3px 6px; color: %4; border-radius: 4px; }
#aiReviewBar QToolButton:hover { background-color: %2; }
#aiReviewBar QToolButton#keep { color: #ffffff; background-color: #2f8f4e; }
#aiReviewBar QToolButton#keep:hover { background-color: #37a65b; }
)").arg(dark ? "#2b2d31" : "#ffffff", dark ? "#45484d" : "#d0d7de", dark ? "#9da0a6" : "#57606a", dark ? "#dfe1e5" : "#24292f"));

    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 3, 4, 3);
    layout->setSpacing(2);

    review->label = new QLabel(bar);
    auto button = [bar](const QString &text, const QString &tip) {
        QToolButton *b = new QToolButton(bar);
        b->setText(text);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    QToolButton *previous = button("▲", tr("Previous change"));
    QToolButton *next = button("▼", tr("Next change"));
    QToolButton *keep = button(tr("Keep"), tr("Keep this change"));
    keep->setObjectName("keep");
    QToolButton *undo = button(tr("Undo"), tr("Undo this change"));
    QToolButton *keepAll = button(tr("Keep file"), tr("Keep all changes in this file"));
    QToolButton *undoAll = button(tr("Undo file"), tr("Undo all changes in this file"));

    layout->addWidget(review->label);
    layout->addWidget(previous);
    layout->addWidget(next);
    layout->addSpacing(6);
    layout->addWidget(keep);
    layout->addWidget(undo);
    layout->addSpacing(6);
    layout->addWidget(keepAll);
    layout->addWidget(undoAll);
    review->bar = bar;

    QPointer<ScintillaNext> target = editor;
    connect(previous, &QToolButton::clicked, this, [this, target]() {
        if (Review *r = find(target))
            goToHunk(r, (r->current - 1 + r->hunks.size()) % r->hunks.size());
    });
    connect(next, &QToolButton::clicked, this, [this, target]() {
        if (Review *r = find(target))
            goToHunk(r, (r->current + 1) % r->hunks.size());
    });
    connect(keep, &QToolButton::clicked, this, [this, target]() {
        if (Review *r = find(target))
            acceptHunk(r, r->current);
    });
    connect(undo, &QToolButton::clicked, this, [this, target]() {
        if (Review *r = find(target))
            rejectHunk(r, r->current);
    });
    connect(keepAll, &QToolButton::clicked, this, [this, target]() { accept(target); });
    connect(undoAll, &QToolButton::clicked, this, [this, target]() { reject(target); });
}

void AiReviewManager::positionBar(Review *review)
{
    ScintillaNext *editor = review->editor;
    // Bottom centre, like Cursor, so it does not cover the top of the file
    const int scrollBar = editor->verticalScrollBar()->isVisible() ? editor->verticalScrollBar()->width() : 0;
    const int hScrollBar = editor->horizontalScrollBar()->isVisible() ? editor->horizontalScrollBar()->height() : 0;
    const int x = qMax(0, (editor->width() - scrollBar - review->bar->width()) / 2);
    const int y = qMax(0, editor->height() - hScrollBar - review->bar->height() - 16);
    review->bar->move(x, y);
}

bool AiReviewManager::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Resize) {
        if (Review *review = find(qobject_cast<ScintillaNext *>(obj)))
            positionBar(review);
    }
    return QObject::eventFilter(obj, event);
}

void AiReviewManager::goToHunk(Review *review, int hunk)
{
    review->current = hunk;
    const int line = review->hunks[hunk].newLine;
    review->editor->gotoLine(line);
    review->editor->setFirstVisibleLine(qMax(0, line - 3));
    review->label->setText(tr("AI change %1 of %2").arg(hunk + 1).arg(review->hunks.size()));
}

void AiReviewManager::acceptHunk(Review *review, int hunk)
{
    // Keeping a change means it becomes part of the baseline
    QList<bool> useNew(review->hunks.size(), false);
    useNew[hunk] = true;
    review->original = AiDiff::rebuild(review->ops, review->hunks, useNew);
    refresh(review);
}

void AiReviewManager::rejectHunk(Review *review, int hunk)
{
    QList<bool> useNew(review->hunks.size(), true);
    useNew[hunk] = false;
    setEditorText(review->editor, AiDiff::rebuild(review->ops, review->hunks, useNew));
    refresh(review);
}

void AiReviewManager::accept(ScintillaNext *editor)
{
    if (Review *review = find(editor))
        finish(review);
}

void AiReviewManager::reject(ScintillaNext *editor)
{
    Review *review = find(editor);
    if (!review)
        return;

    if (review->newFile && editor->isFile()) {
        // The AI created this file: remove it again
        const QString path = editor->getFilePath();
        finish(review);
        editor->close();
        QFile::remove(path);
        return;
    }

    setEditorText(editor, review->original);
    finish(review);
}

void AiReviewManager::acceptAll()
{
    const QList<Review *> all = reviews;
    for (Review *review : all)
        accept(review->editor);
}

void AiReviewManager::rejectAll()
{
    const QList<Review *> all = reviews;
    for (Review *review : all)
        reject(review->editor);
}
