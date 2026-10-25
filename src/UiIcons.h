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

#include <QColor>
#include <QIcon>

// Small icons drawn in code so they stay crisp at any scale and follow the theme
namespace UiIcons
{
    enum Glyph {
        NewFile,
        NewFolder,
        Refresh,
        CollapseAll,
        FolderOpen,
        More,
        Close,
        Terminal,
        Trash,
        Search,
    };

    // Icon for a file in the explorer and on editor tabs, coloured by language
    QIcon fileIcon(const QString &fileName);
    QIcon folderIcon(bool open = false);

    // Outline glyph for tool buttons; an invalid colour uses the palette text colour
    QIcon glyph(Glyph glyph, QColor colour = QColor());

    // Filled circle, used on tabs with unsaved changes
    QIcon dotIcon(const QColor &colour);
}
