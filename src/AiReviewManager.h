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

#include "AiDiff.h"

#include <QList>
#include <QObject>
#include <QPointer>

class MainWindow;
class QFrame;
class QLabel;
class ScintillaNext;

// Shows AI changes inside the editor like Cursor: added lines get a green
// background, removed lines appear as red annotations, and a floating bar
// lets the user step through the changes and keep or undo each of them.
// The file on disk already contains the change; undoing writes it back.
class AiReviewManager : public QObject
{
    Q_OBJECT

public:
    struct FileSummary {
        ScintillaNext *editor;
        QString name;
        int added;
        int removed;
    };

    explicit AiReviewManager(MainWindow *window);

    // Puts newText into the editor (undoable, saved) and reviews it against the text before the first AI change
    void applyToEditor(ScintillaNext *editor, const QString &newText, bool newFile = false);

    // The editor was already changed; review it against 'original'
    void startReview(ScintillaNext *editor, const QString &original);

    bool isUnderReview(ScintillaNext *editor) const;
    QList<FileSummary> summaries() const;

    void accept(ScintillaNext *editor);
    void reject(ScintillaNext *editor);
    void acceptAll();
    void rejectAll();

    // Annotation styles shared with the autocomplete ghost text: base + 0 = ghost, base + 1 = removed lines
    static int annotationStyles(ScintillaNext *editor);

signals:
    void changed();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    struct Review {
        QPointer<ScintillaNext> editor;
        QString original;
        bool newFile = false;
        QList<AiDiff::Op> ops;
        QList<AiDiff::Hunk> hunks;
        QList<int> annotatedLines;
        int current = 0;
        QFrame *bar = nullptr;
        QLabel *label = nullptr;
    };

    Review *find(ScintillaNext *editor) const;
    Review *create(ScintillaNext *editor, const QString &original);
    void refresh(Review *review);
    void finish(Review *review);
    void decorate(Review *review);
    void clearDecorations(Review *review);
    void createBar(Review *review);
    void positionBar(Review *review);
    void goToHunk(Review *review, int hunk);
    void acceptHunk(Review *review, int hunk);
    void rejectHunk(Review *review, int hunk);

    MainWindow *window;
    QList<Review *> reviews;
};
