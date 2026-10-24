# NotepadAI

**Notepad++ hissinde, AI asistanı ve dosya yöneticisi gömülü, hafif ve çapraz platform (Windows / Linux / macOS) kod editörü.**

[Notepad Next](https://github.com/dail8859/NotepadNext) (Notepad++'ın Qt ile yeniden yazılmış hali) üzerine kurulmuş bir forktur. Amaç: Antigravity / Cursor gibi AI IDE'lerin işlevini, Electron ağırlığı olmadan, Notepad++ hızında sunmak.

![NotepadAI](doc/notepadai-screenshot.png)

## Özellikler

- 🤖 **AI Asistan paneli** (`View → AI Assistant`, `Ctrl+Shift+I`)
  - **Claude** (Anthropic), **OpenAI** ve tüm **OpenAI-uyumlu** servisler: **Ollama**, LM Studio, OpenRouter, Groq...
  - Yanıtlar canlı akar (streaming)
  - Seçili kod ya da açık dosya otomatik olarak bağlam olarak gönderilir
  - **Explain**: seçili kodu tek tıkla açıklatır
  - **Insert Code**: yanıttaki kodu editöre ekler (seçim varsa onun yerine yazar)
  - `Ctrl+Enter` ile gönderme, **Stop** ile durdurma, **New Chat** ile sıfırlama
- 📁 **Dosya yöneticisi** (Folder as Workspace) varsayılan olarak açık
- ✍️ Notepad++'tan alışık olunan her şey: Scintilla editörü, sözdizimi renklendirme, makrolar, çoklu imleç, arama/değiştirme, Lua betikleri

## AI Ayarları

Panelde **Settings** düğmesine tıklayın:

| Sağlayıcı | Provider | Base URL | Model örneği |
|---|---|---|---|
| Claude | Anthropic | `https://api.anthropic.com` | `claude-sonnet-5-5` |
| OpenAI | OpenAI-compatible | `https://api.openai.com/v1` | `gpt-4o-mini` |
| Ollama (yerel, ücretsiz) | OpenAI-compatible | `http://localhost:11434/v1` | `qwen2.5-coder` |
| OpenRouter | OpenAI-compatible | `https://openrouter.ai/api/v1` | `anthropic/claude-sonnet-5-5` |

> ⚠️ API anahtarı yerel ayar dosyasında düz metin olarak saklanır (`~/.config/NotepadAI/NotepadAI.ini`, Windows'ta `%APPDATA%\NotepadAI\NotepadAI.ini`).

## İndirme

Her push'ta GitHub Actions; Windows, Linux (AppImage) ve macOS için derlenmiş sürümleri üretir: **Actions → son çalışma → Artifacts**.

## Kaynaktan derleme

Gerekenler: Qt 6.5+ (Core, Gui, Widgets, Network, PrintSupport, Core5Compat), CMake 3.21+, C++20 derleyici.

```bash
# Ubuntu / Debian
sudo apt install qt6-base-dev qt6-base-private-dev qt6-5compat-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libgl-dev ninja-build cmake g++

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
./build/src/NotepadNext
```

Windows/macOS için ayrıntılı talimatlar: [doc/Building.md](doc/Building.md)

## Yol Haritası

- [ ] Yanıttaki değişiklikleri **diff** olarak gösterip onaylayarak uygulama
- [ ] Satır içi (inline) AI düzenleme: seçimi işaretle → `Ctrl+K` → talimat yaz
- [ ] Çoklu dosya bağlamı (açık sekmeler / klasör)
- [ ] Agent modu: dosya okuma/yazma ve terminal komutu çalıştırma (onaylı)
- [ ] Kod tamamlama (ghost text)
- [ ] API anahtarını işletim sisteminin anahtar zincirinde saklama
- [ ] Koyu tema

## Lisans

GPL-3.0. Orijinal Notepad Next © Justin Dailey ve katkıda bulunanlar. Orijinal README: [doc/NotepadNext-README.md](doc/NotepadNext-README.md)
