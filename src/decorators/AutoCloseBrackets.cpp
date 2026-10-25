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

#include "AutoCloseBrackets.h"

#include <QKeyEvent>

namespace {

char closerFor(char ch)
{
    switch (ch) {
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    case '"': return '"';
    case '\'': return '\'';
    case '`': return '`';
    default: return 0;
    }
}

bool isCloser(char ch)
{
    return ch == ')' || ch == ']' || ch == '}';
}

bool isQuote(char ch)
{
    return ch == '"' || ch == '\'' || ch == '`';
}

bool isWordChar(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || (static_cast<unsigned char>(ch) >= 0x80);
}

// A pair is only inserted when nothing that belongs to a word follows the caret
bool allowsPairBefore(char next)
{
    return next == 0 || next == ' ' || next == '\t' || next == '\r' || next == '\n' || isCloser(next) || next == ',' || next == ';' || next == ':' || next == '.';
}

}

AutoCloseBrackets::AutoCloseBrackets(ScintillaNext *editor) :
    EditorDecorator(editor)
{
    setObjectName("AutoCloseBrackets");

    editor->installEventFilter(this);
}

bool AutoCloseBrackets::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj)

    if (!enabled || event->type() != QEvent::KeyPress)
        return false;

    if (editor->readOnly() || editor->selections() > 1 || !editor->selectionEmpty() || editor->autoCActive())
        return false;

    return handleKey(static_cast<QKeyEvent *>(event));
}

bool AutoCloseBrackets::handleKey(QKeyEvent *keyEvent)
{
    const Qt::KeyboardModifiers modifiers = keyEvent->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    if (modifiers != Qt::NoModifier)
        return false;

    const int pos = editor->currentPos();
    const int line = editor->lineFromPosition(pos);
    if (line != pendingLine) {
        pendingLine = line;
        pendingCount = 0;
    }

    const char prev = pos > 0 ? static_cast<char>(editor->charAt(pos - 1)) : 0;
    const char next = static_cast<char>(editor->charAt(pos));

    if (keyEvent->key() == Qt::Key_Backspace) {
        if (pendingCount > 0 && closerFor(prev) != 0 && closerFor(prev) == next) {
            editor->setTargetRange(pos - 1, pos + 1);
            editor->replaceTarget(0, "");
            --pendingCount;
            return true;
        }
        return false;
    }

    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        // {|} + Enter puts the caret on its own indented line between the brackets
        if (!isCloser(next) || closerFor(prev) != next)
            return false;

        const int baseIndent = editor->lineIndentation(line);
        const int step = editor->indent() > 0 ? editor->indent() : editor->tabWidth();

        editor->beginUndoAction();
        editor->newLine();
        editor->newLine();
        editor->setLineIndentation(line + 2, baseIndent);
        editor->setLineIndentation(line + 1, baseIndent + step);
        editor->gotoPos(editor->lineIndentPosition(line + 1));
        editor->endUndoAction();

        pendingCount = 0;
        return true;
    }

    const QString text = keyEvent->text();
    if (text.size() != 1 || text.at(0).unicode() > 0x7f)
        return false;

    const char ch = text.at(0).toLatin1();

    // Type over a closer that was inserted automatically
    if ((isCloser(ch) || isQuote(ch)) && next == ch && pendingCount > 0) {
        editor->gotoPos(pos + 1);
        --pendingCount;
        return true;
    }

    const char closer = closerFor(ch);
    if (closer == 0 || !allowsPairBefore(next))
        return false;

    // Quotes after a letter are apostrophes (don't) or closing quotes
    if (isQuote(ch) && (isWordChar(prev) || prev == ch))
        return false;

    const char pair[3] = {ch, closer, 0};
    editor->beginUndoAction();
    editor->replaceSel(pair);
    editor->gotoPos(pos + 1);
    editor->endUndoAction();
    ++pendingCount;
    return true;
}

void AutoCloseBrackets::notify(const Scintilla::NotificationData *pscn)
{
    Q_UNUSED(pscn)
}
