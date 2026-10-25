# NotepadAI — agent guide

NotepadAI is a fork of Notepad Next (a Qt 6 / C++20 reimplementation of Notepad++) with an embedded AI assistant.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # set QMAKE=$(which qmake6) if qmake isn't found
ninja -C build
./build/src/NotepadNext [file]
```

Always rebuild after a change and fix every compiler error before finishing. Headless GUI checks: run under `Xvfb :99` with `DISPLAY=:99`, drive with `xdotool`, screenshot with `import -window root shot.png`.

## Layout

- `src/dialogs/MainWindow.cpp` — main window; docks are created and added to the View menu in its constructor.
- `src/AiClient.*` — provider settings (`AI/` QSettings group), settings dialog, OpenRouter PKCE browser login, non-streaming `complete()` used by inline features.
- `src/AiInlineAssist.*` — Ctrl+I inline edit and ghost-text autocomplete (Scintilla EOL annotations + annotations, Tab accepts).
- `src/DarkTheme.*` — Fusion dark palette, extra style sheet, and conversion of lexer styles to dark (called at the end of `setEditorLanguage`). Light colours live in `EditorManager::applyColors()`.
- `src/AiDiff.*` — line diff (LCS), hunks and rebuilding a text from chosen hunks.
- `src/AiReviewManager.*` — in-editor review of AI changes: markers 18/19 for added lines, annotations for removed lines (shared annotation styles with ghost text via `annotationStyles()`), floating Keep/Undo bar. Agent edits and Ctrl+I go through it.
- `src/AiAgentTools.*` — agent tools (list/read/search/edit/write files, run_command), path sandboxing to the workspace root, diff approval dialog.
- `src/docks/` — dock panels. `AiChatDock.*` is the AI assistant (chat UI, streaming, agent loop); `FolderAsWorkspaceDock.*` is the file tree.
- `src/ScintillaNext.*` — editor widget (Scintilla). Use `getSelText()`, `getText(textLength() + 1)`, `replaceSel()`. Note `ScintillaEdit::setFocus(bool)` shadows `QWidget::setFocus()`; call `editor->QWidget::setFocus()`.
- `thirdparty/` — vendored libraries (Scintilla, Lexilla, Lua, ...). Do not modify.
- New source files must be listed in `src/CMakeLists.txt`.

## AI assistant (`src/docks/AiChatDock.cpp`)

- Settings live in `QSettings` under the `AI/` group (`Provider`, `BaseUrl`, `ApiKey`, `Model`). Settings are INI: `~/.config/NotepadAI/NotepadAI.ini`.
- `Provider=anthropic` → `POST {base}/v1/messages` with SSE `content_block_delta` events.
- `Provider=openai` → `POST {base}/chat/completions` with SSE `choices[0].delta.content` (OpenAI, Ollama, LM Studio, OpenRouter).
- History is stored provider-neutral (`user`, `assistant` with `tool_calls`, `tool`) and converted per request by `anthropicMessages()` / `openAiMessages()`.
- To test without a real key, run a local mock SSE server and point `BaseUrl` at it.

## Conventions

- Match the existing Qt style: `tr()` for user-visible strings, signals/slots via `connect` with member pointers, new files carry the GPL header.
- Keep commits small, one feature per commit.

## UI and translations

- `src/docks/AiChatView.*` renders the conversation as widgets (user bubbles, Markdown text, code blocks with Copy/Insert, one-line tool steps, empty state). `AiChatDock` owns the logic, title bar and composer.
- Panel colours live in `DarkTheme::styleSheet()` (both themes) and `AiChatView::refreshStyle()`.
- User-visible strings are English in `tr()`; Turkish translations are in `i18n/NotepadNext_tr.ts` (add new strings there; avoid `%n` plurals, use `%1` with `.arg()`). Translation files keep the `NotepadNext_` prefix.
