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

#include <QList>
#include <QString>
#include <QStringList>

// Line based diff used by the diff preview and the in-editor review.
namespace AiDiff
{
    struct Op {
        char type;     // ' ' unchanged, '-' only in the old text, '+' only in the new text
        QString line;
    };

    // A run of changed ops (no unchanged lines inside)
    struct Hunk {
        int firstOp;
        int lastOp;      // inclusive
        int newLine;     // first line of the hunk in the new text (where '+' lines start)
        int added;
        int removed;
    };

    // Lossless split: lines(text).join('\n') == text
    QStringList lines(const QString &text);

    QList<Op> diff(const QStringList &oldLines, const QStringList &newLines);
    QList<Hunk> hunks(const QList<Op> &ops);

    // Rebuild a text from the ops, taking the new side for hunks in 'useNew' and the old side elsewhere
    QString rebuild(const QList<Op> &ops, const QList<Hunk> &hunks, const QList<bool> &useNew);
}
