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

#include "AiInlineAssist.h"

#include "AiAgentTools.h"
#include "AiClient.h"
#include "AiReviewManager.h"
#include "MainWindow.h"
#include "ScintillaNext.h"

#include <QAction>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const char *EDIT_PROMPT =
    "You edit code inside a text editor. You get an excerpt of a file with the selected code marked, and an instruction. "
    "Return ONLY the new code that replaces the selected code: no explanations, no markdown code fences. "
    "Keep the surrounding indentation style. If the selection is empty, return the code to insert.";

const char *COMPLETE_PROMPT =
    "You are a code autocomplete engine. Output ONLY the exact text to insert at <CURSOR>: "
    "no explanations, no markdown code fences, and never repeat code that is already before or after the cursor. "
    "Prefer finishing the current line or adding the next few lines (at most 8). "
    "If there is no useful completion, output nothing.";

const int EDIT_CONTEXT_BEFORE = 3000;
const int EDIT_CONTEXT_AFTER = 2000;
const int COMPLETE_CONTEXT_BEFORE = 4000;
const int COMPLETE_CONTEXT_AFTER = 1500;
const int COMPLETE_DELAY_MS = 350;
const int COMPLETE_MAX_LINES = 8;

QString stripCodeFences(QString text)
{
    static const QRegularExpression fenced("^\\s*```[^\\n]*\\n(.*?)\\n?```\\s*$", QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = fenced.match(text);
    if (match.hasMatch())
        text = match.captured(1);
    return text;
}

QString textRange(ScintillaNext *editor, int start, int end)
{
    return QString::fromUtf8(editor->get_text_range(start, end));
}

}

AiInlineAssist::AiInlineAssist(MainWindow *window) :
    QObject(window),
    window(window),
    client(new AiClient(this)),
    debounce(new QTimer(this))
{
    editAction = new QAction(tr("AI Edit Selection..."), this);
    editAction->setShortcut(QKeySequence("Ctrl+I"));
    editAction->setToolTip(tr("Describe a change to the selected code (or current line) and review the result"));
    connect(editAction, &QAction::triggered, this, &AiInlineAssist::inlineEdit);

    completeAction = new QAction(tr("AI Autocomplete"), this);
    completeAction->setCheckable(true);
    completeAction->setChecked(QSettings().value("AI/Autocomplete", true).toBool());
    connect(completeAction, &QAction::toggled, this, [this](bool checked) {
        QSettings().setValue("AI/Autocomplete", checked);
        if (!checked)
            dismissSuggestion();
    });

    debounce->setSingleShot(true);
    debounce->setInterval(COMPLETE_DELAY_MS);
    connect(debounce, &QTimer::timeout, this, &AiInlineAssist::requestCompletion);
}

void AiInlineAssist::watchEditor(ScintillaNext *editor)
{
    editor->installEventFilter(this);

    connect(editor, &ScintillaNext::charAdded, this, [this, editor](int) {
        scheduleCompletion(editor);
    });
    connect(editor, &ScintillaNext::updateUi, this, [this, editor](Scintilla::Update) {
        if (ghostEditor == editor && editor->currentPos() != ghostPos)
            dismissSuggestion();
    });
    connect(editor, &ScintillaNext::focusChanged, this, [this, editor](bool focused) {
        if (!focused && ghostEditor == editor)
            dismissSuggestion();
    });
}

bool AiInlineAssist::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && ghostEditor && obj == ghostEditor) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier) {
            acceptSuggestion();
            return true;
        }
        // Ctrl+Right takes the suggestion one word at a time
        if (keyEvent->key() == Qt::Key_Right && keyEvent->modifiers() == Qt::ControlModifier) {
            acceptSuggestion(false);
            return true;
        }
        const int key = keyEvent->key();
        if (key != Qt::Key_Shift && key != Qt::Key_Control && key != Qt::Key_Alt && key != Qt::Key_Meta)
            dismissSuggestion();
    }
    return QObject::eventFilter(obj, event);
}

void AiInlineAssist::inlineEdit()
{
    ScintillaNext *editor = window->currentEditor();
    if (!editor)
        return;

    if (!AiClient::isConfigured(AiClient::load()) && (!AiClient::showSettings(window) || !AiClient::isConfigured(AiClient::load())))
        return;

    dismissSuggestion();

    // Work on whole lines so the model sees complete statements
    int start = editor->selectionStart();
    int end = editor->selectionEnd();
    start = editor->positionFromLine(editor->lineFromPosition(start));
    if (end == start || editor->column(end) != 0 || end == editor->selectionStart())
        end = editor->lineEndPosition(editor->lineFromPosition(end));

    const QString original = textRange(editor, start, end);
    const int length = editor->textLength();
    const QString before = textRange(editor, qMax(0, start - EDIT_CONTEXT_BEFORE), start);
    const QString after = textRange(editor, end, qMin<int>(length, end + EDIT_CONTEXT_AFTER));

    QDialog dialog(window, Qt::Tool | Qt::FramelessWindowHint);
    dialog.setStyleSheet("QDialog { border: 1px solid palette(highlight); }");
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    QLineEdit *instruction = new QLineEdit(&dialog);
    instruction->setPlaceholderText(tr("Describe the change, e.g. \"add error handling\" (Enter to run, Esc to cancel)"));
    instruction->setMinimumWidth(520);
    QLabel *status = new QLabel(&dialog);
    status->setStyleSheet("color: gray;");
    status->hide();
    layout->addWidget(instruction);
    layout->addWidget(status);

    const int line = editor->lineFromPosition(start);
    const QPoint anchor(editor->pointXFromPosition(start), editor->pointYFromPosition(start) + editor->textHeight(line));
    dialog.move(editor->mapToGlobal(anchor));

    QString result;
    QString error;
    QPointer<QNetworkReply> request;

    connect(instruction, &QLineEdit::returnPressed, &dialog, [&]() {
        if (request || instruction->text().trimmed().isEmpty())
            return;
        instruction->setEnabled(false);
        status->setText(tr("Generating..."));
        status->show();

        const QString prompt = QString("File: %1 (%2)\n\nCode before the selection:\n```\n%3\n```\n\n"
                                       "Selected code to rewrite:\n```\n%4\n```\n\n"
                                       "Code after the selection:\n```\n%5\n```\n\nInstruction: %6")
                                   .arg(editor->getName(), editor->languageName, before, original, after, instruction->text());
        request = client->complete(EDIT_PROMPT, prompt, 8000, false, [&](const QString &text, const QString &err) {
            result = text;
            error = err;
            dialog.accept();
        });
    });
    connect(&dialog, &QDialog::rejected, &dialog, [&]() {
        if (request)
            request->abort();
    });

    instruction->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (!error.isEmpty()) {
        QMessageBox::warning(window, tr("AI Edit"), error);
        return;
    }

    QString replacement = stripCodeFences(result);
    if (!original.endsWith('\n') && replacement.endsWith('\n'))
        replacement.chop(1);

    QPointer<ScintillaNext> target = editor;
    if (!target || textRange(target, start, end) != original) {
        QMessageBox::warning(window, tr("AI Edit"), tr("The text changed while the AI was working. Try again."));
        return;
    }

    // Apply in place and show the change as an in-editor diff to keep or undo
    const QString wholeTextBefore = QString::fromUtf8(target->getText(target->textLength() + 1));
    const QByteArray data = replacement.toUtf8();
    target->beginUndoAction();
    target->setTargetRange(start, end);
    target->replaceTarget(data.size(), data.constData());
    target->endUndoAction();
    target->gotoPos(start);
    target->QWidget::setFocus();

    if (reviews)
        reviews->startReview(target, wholeTextBefore);
}

void AiInlineAssist::scheduleCompletion(ScintillaNext *editor)
{
    ++generation;
    if (completionReply)
        completionReply->abort();
    dismissSuggestion();

    if (!QSettings().value("AI/Autocomplete", true).toBool())
        return;

    typingEditor = editor;
    debounce->start();
}

void AiInlineAssist::requestCompletion()
{
    ScintillaNext *editor = typingEditor;
    if (!editor || editor != window->currentEditor() || !editor->hasFocus())
        return;
    if (!editor->selectionEmpty() || editor->selections() > 1 || editor->autoCActive())
        return;

    const AiClient::Provider provider = AiClient::load();
    if (!AiClient::isConfigured(provider))
        return;
    // Ghost text and the review's removed-line annotations share the annotation area
    if (reviews && reviews->isUnderReview(editor))
        return;

    // Only suggest at the end of a line, where the ghost text can be shown in place
    const int pos = editor->currentPos();
    const int line = editor->lineFromPosition(pos);
    if (!textRange(editor, pos, editor->lineEndPosition(line)).trimmed().isEmpty())
        return;

    const QString before = textRange(editor, qMax(0, pos - COMPLETE_CONTEXT_BEFORE), pos);
    const QString after = textRange(editor, pos, qMin<int>(editor->textLength(), pos + COMPLETE_CONTEXT_AFTER));
    if (before.trimmed().isEmpty())
        return;

    const QString prompt = QString("File: %1 (%2)\n<code>\n%3<CURSOR>%4\n</code>").arg(editor->getName(), editor->languageName, before, after);
    const int requestGeneration = generation;
    QPointer<ScintillaNext> target = editor;

    completionReply = client->complete(COMPLETE_PROMPT, prompt, 256, true, [=, this](const QString &text, const QString &error) {
        if (!error.isEmpty() || requestGeneration != generation || !target || target->currentPos() != pos)
            return;

        QString suggestion = stripCodeFences(text);
        while (suggestion.endsWith('\n') || suggestion.endsWith(' ') || suggestion.endsWith('\t'))
            suggestion.chop(1);
        QStringList lines = suggestion.split('\n');
        if (lines.size() > COMPLETE_MAX_LINES)
            suggestion = lines.mid(0, COMPLETE_MAX_LINES).join('\n');
        if (suggestion.trimmed().isEmpty() || after.trimmed().startsWith(suggestion.trimmed()))
            return;

        showSuggestion(target, pos, suggestion);
    });
}

void AiInlineAssist::showSuggestion(ScintillaNext *editor, int pos, const QString &text)
{
    dismissSuggestion();

    // Style 0 of the shared annotation styles is the grey ghost text
    AiReviewManager::annotationStyles(editor);

    const int line = editor->lineFromPosition(pos);
    const QStringList lines = text.split('\n');
    if (!lines.first().isEmpty()) {
        editor->eOLAnnotationSetText(line, lines.first().toUtf8().constData());
        editor->eOLAnnotationSetStyle(line, 0);
    }
    if (lines.size() > 1) {
        editor->annotationSetText(line, lines.mid(1).join('\n').toUtf8().constData());
        editor->annotationSetStyle(line, 0);
    }

    ghostEditor = editor;
    ghostPos = pos;
    ghostLine = line;
    ghostText = text;
}

void AiInlineAssist::acceptSuggestion(bool wholeSuggestion)
{
    QPointer<ScintillaNext> editor = ghostEditor;
    const int pos = ghostPos;
    QString text = ghostText;
    QString rest;
    if (!wholeSuggestion) {
        static const QRegularExpression word("^(\\s*[\\w]+|\\s*[^\\w\\s]+|\\s+)");
        const QRegularExpressionMatch match = word.match(text);
        if (match.hasMatch()) {
            rest = text.mid(match.capturedLength());
            text = match.captured(1);
        }
    }

    const QByteArray data = text.toUtf8();
    dismissSuggestion();
    if (!editor)
        return;

    editor->insertText(pos, data.constData());
    editor->gotoPos(pos + data.size());

    // Keep showing what is left of the suggestion
    if (!rest.isEmpty() && !text.contains('\n'))
        showSuggestion(editor, pos + data.size(), rest);
}

void AiInlineAssist::dismissSuggestion()
{
    if (ghostEditor) {
        ghostEditor->eOLAnnotationSetText(ghostLine, nullptr);
        ghostEditor->annotationSetText(ghostLine, nullptr);
    }
    ghostEditor = nullptr;
    ghostPos = -1;
    ghostLine = -1;
    ghostText.clear();
}
