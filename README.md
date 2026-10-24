# NotepadAI

**Notepad++ hissinde, AI asistanı ve dosya yöneticisi gömülü, hafif ve çapraz platform (Windows / Linux / macOS) kod editörü.**

[Notepad Next](https://github.com/dail8859/NotepadNext) (Notepad++'ın Qt ile yeniden yazılmış hali) üzerine kurulmuş bir forktur. Amaç: Antigravity / Cursor gibi AI IDE'lerin işlevini, Electron ağırlığı olmadan, Notepad++ hızında sunmak.

![NotepadAI](doc/notepadai-screenshot.png)

## Özellikler

- 🤖 **Agent modu** (Cursor / Antigravity gibi): AI projeyi kendisi keşfeder, dosyaları okur, düzenler, yeni dosya oluşturur ve komut çalıştırır
  - Araçlar: `list_files`, `read_file`, `search`, `edit_file`, `write_file`, `run_command`
  - Her dosya değişikliği önce **renkli diff** olarak gösterilir, **Accept / Reject** ile onaylarsın (istersen *Auto-apply edits* ile otomatik)
  - Terminal komutları **her zaman** onay ister
  - AI sadece açık çalışma klasörünün içine erişebilir, dışına yazamaz
  - Açık dosyalardaki değişiklikler `Ctrl+Z` ile geri alınabilir
- 💬 **Sohbet modu**: seçili kodu/dosyayı bağlam olarak gönderme, **Explain**, **Insert Code**
- 🔌 **Claude** (Anthropic), **OpenAI** ve tüm **OpenAI-uyumlu** servisler: **Ollama**, LM Studio, OpenRouter, Groq... Yanıtlar canlı akar
- 📁 **Dosya yöneticisi** (Folder as Workspace) varsayılan olarak açık
- ✍️ Notepad++'tan alışık olunan her şey: Scintilla editörü, sözdizimi renklendirme, makrolar, çoklu imleç, arama/değiştirme, Lua betikleri

## Kullanım

1. *File → Open Folder as Workspace* ile proje klasörünü aç
2. AI panelinde (`View → AI Assistant`, `Ctrl+Shift+I`) **Settings** → sağlayıcı ve API anahtarı
3. Ne istediğini yaz, `Ctrl+Enter`: *"login formuna e-posta doğrulaması ekle ve testleri çalıştır"*

> Agent modu için araç çağırmayı (tool calling) destekleyen bir model gerekir: Claude, GPT-4o/4.1, Ollama'da `qwen2.5-coder`, `llama3.1` vb.

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

- [x] Agent modu: dosya okuma/yazma, arama ve terminal komutu (onaylı)
- [x] Değişiklikleri **diff** olarak gösterip onaylayarak uygulama
- [ ] Checkpoint: agent'ın tüm değişikliklerini tek tıkla geri alma
- [ ] Satır içi (inline) AI düzenleme: seçimi işaretle → `Ctrl+K` → talimat yaz
- [ ] Çoklu dosya bağlamı (açık sekmeler / klasör)
- [ ] Kod tamamlama (ghost text)
- [ ] API anahtarını işletim sisteminin anahtar zincirinde saklama
- [ ] Koyu tema

## Lisans

GPL-3.0. Orijinal Notepad Next © Justin Dailey ve katkıda bulunanlar. Orijinal README: [doc/NotepadNext-README.md](doc/NotepadNext-README.md)
