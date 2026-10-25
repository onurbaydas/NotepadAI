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

#ifndef AUTOCLOSEBRACKETS_H
#define AUTOCLOSEBRACKETS_H

#include "EditorDecorator.h"

// Inserts the closing bracket or quote when the opening one is typed, types over
// it when it is typed again, deletes both with Backspace and splits {} over lines on Enter
class AutoCloseBrackets : public EditorDecorator
{
    Q_OBJECT

public:
    explicit AutoCloseBrackets(ScintillaNext *editor);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

public slots:
    void notify(const Scintilla::NotificationData *pscn) override;

private:
    bool handleKey(class QKeyEvent *keyEvent);

    // Closers inserted on pendingLine that have not been typed over yet
    int pendingLine = -1;
    int pendingCount = 0;
};

#endif // AUTOCLOSEBRACKETS_H
