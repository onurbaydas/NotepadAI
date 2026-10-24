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
- `src/docks/` — dock panels. `AiChatDock.*` is the AI assistant; `FolderAsWorkspaceDock.*` is the file tree.
- `src/ScintillaNext.*` — editor widget (Scintilla). Use `getSelText()`, `getText(textLength() + 1)`, `replaceSel()`. Note `ScintillaEdit::setFocus(bool)` shadows `QWidget::setFocus()`; call `editor->QWidget::setFocus()`.
- `thirdparty/` — vendored libraries (Scintilla, Lexilla, Lua, ...). Do not modify.
- New source files must be listed in `src/CMakeLists.txt`.

## AI assistant (`src/docks/AiChatDock.cpp`)

- Settings live in `QSettings` under the `AI/` group (`Provider`, `BaseUrl`, `ApiKey`, `Model`). Settings are INI: `~/.config/NotepadAI/NotepadAI.ini`.
- `Provider=anthropic` → `POST {base}/v1/messages` with SSE `content_block_delta` events.
- `Provider=openai` → `POST {base}/chat/completions` with SSE `choices[0].delta.content` (OpenAI, Ollama, LM Studio, OpenRouter).
- To test without a real key, run a local mock SSE server and point `BaseUrl` at it.

## Conventions

- Match the existing Qt style: `tr()` for user-visible strings, signals/slots via `connect` with member pointers, new files carry the GPL header.
- Keep commits small, one feature per commit.
