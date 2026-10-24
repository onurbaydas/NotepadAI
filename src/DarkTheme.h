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

#include <QString>

class ScintillaNext;

// Dark mode for the whole application. The editor colors come from the
// per-language light themes and are converted on the fly, so every language
// gets a dark variant without maintaining a second set of styles.
namespace DarkTheme
{
    bool isEnabled();
    void setEnabled(bool enabled);

    // Qt palette and widget style
    void applyToApplication();

    // Additions to the application style sheet
    QString styleSheet();

    // Convert the editor's current (light) styles; call after the language styles are set
    void applyToEditor(ScintillaNext *editor);
}
