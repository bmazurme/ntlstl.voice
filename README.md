# ESP32 → STT → LLM → HA

Voice control pipeline: a wake-word-triggered ESP32-S3 (or, for now, a
browser mic) records a short voice command → [whisper.cpp](https://github.com/ggerganov/whisper.cpp)
transcribes it → a local LLM ([Ollama](https://ollama.com)) turns the
transcript into a structured smart-home command → the command is executed
in [Home Assistant](https://www.home-assistant.io/).

The `frontend/` React app is a debugging tool: it drives the exact same
backend the ESP32 will, but shows the result of each stage and requires
an explicit click to move to the next one (record → confirm → confirm →
execute), so you can catch bad transcriptions or bad LLM output before
anything actually happens in Home Assistant.

## Возможности

- Запись голосовой команды с браузера или физической кнопки на ESP32 (S3/P4)
- Локальный STT через whisper.cpp — аудио не покидает вашу сеть
- Локальный LLM (Ollama) переводит транскрипт в структурированную команду
  для Home Assistant, зная актуальный список сущностей
- Пошаговое подтверждение каждой стадии во frontend для отладки без риска
  выполнить случайную команду
- Автономная прошивка ESP32-P4 проходит весь пайплайн (запись → STT → LLM →
  HA) без браузера
- Локальный TTS (Piper) озвучивает `response_text` через колонку ESP32-P4
- Готовый docker-compose и инструкция для развёртывания на Raspberry Pi 5

## Архитектура

```
React (frontend, Vite, :5173)                         esp32-client/ (ESP-IDF firmware)
   │ 1. POST /api/transcribe (multipart audio)            │ wake word (on-device) → record → POST audio
   ▼                                                       ▼
Node/Express + TypeScript (backend, :3001) ◄───────────────┘
   │  ffmpeg: webm/opus → wav 16kHz mono
   │  whisper-cli -m model.bin -f audio.wav  →  transcript
   │
   │ 2. POST /api/llm-command { text }
   │  systemPrompt = system_prompt (repo root) + real entities from
   │                 GET {HA_URL}/api/states
   │  Ollama /api/chat (format: json)  →  { action, entity, value, response_text }
   │
   │ 3. POST /api/ha-execute { action, entity, value, response_text }
   │  turn_on/turn_off/toggle/set_brightness → POST {HA_URL}/api/services/<domain>/<service>
   │
   │ 4. POST /api/tts { text: response_text }        (ESP32-P4 only, best-effort)
   │  piper -m voice.onnx -f out.wav  →  WAV audio, played through the P4's speaker
   ▼
Home Assistant
```

Браузер не может напрямую вызвать бинарник whisper.cpp, поэтому backend
выступает прослойкой: принимает аудио, конвертирует его в WAV через ffmpeg
(whisper.cpp не понимает webm/opus, который обычно записывает браузер) и
вызывает `whisper-cli` как подпроцесс. По той же причине ESP32 (см.
`esp32-client/`) шлёт аудио на тот же `/api/transcribe`.

`system_prompt` (в корне репозитория) — единственный источник правды для
того, как LLM должна превращать голосовую команду в JSON-команду. Backend
дополняет его реальным списком сущностей из Home Assistant перед каждым
запросом к Ollama, чтобы LLM не придумывала entity_id.

## Стек технологий

- **frontend/** — React, Vite
- **backend/** — Node.js, TypeScript, Express
- **esp32-client/**, **esp32-p4-wifi6/** — прошивки на ESP-IDF (C)
- STT — [whisper.cpp](https://github.com/ggerganov/whisper.cpp) (через `whisper-cli` как подпроцесс)
- LLM — [Ollama](https://ollama.com), любая модель с поддержкой JSON-режима
- TTS — [Piper](https://github.com/rhasspy/piper) (через бинарник как подпроцесс), только для ESP32-P4
- Умный дом — [Home Assistant](https://www.home-assistant.io/) REST API
- Развёртывание — Docker Compose (см. `docker-compose.yml.example`) либо вручную на Raspberry Pi 5 (см. `DEPLOY-RPI5.md`)

## Требования

- Node.js 18+
- [ffmpeg](https://ffmpeg.org/) — `brew install ffmpeg`
- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) — `brew install whisper-cpp`
  (бинарник называется `whisper-cli`)
- GGML-модель whisper, например мультиязычная `ggml-small.bin`:
  ```bash
  curl -L -o backend/models/ggml-small.bin \
    https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin
  ```
- [Ollama](https://ollama.com) с загруженной моделью, поддерживающей JSON-режим, например:
  ```bash
  ollama pull llama3
  ```
- Работающий Home Assistant + Long-Lived Access Token (Профиль → Безопасность
  → Долгосрочные токены доступа)
- (опционально, для озвучки ответов на ESP32-P4) [Piper](https://github.com/rhasspy/piper) —
  на Linux подходит бинарник с [релизов](https://github.com/rhasspy/piper/releases)
  (`piper_linux_*.tar.gz`); **на macOS релизный `piper_macos_aarch64.tar.gz`
  на самом деле x86_64** (репозиторий archived, никто не починит) и не
  запустится нативно на Apple Silicon — ставьте через PyPI (нормальные
  arm64-wheels):
  ```bash
  python3 -m venv /opt/homebrew/piper-venv
  /opt/homebrew/piper-venv/bin/pip install piper-tts
  curl -L -o backend/models/ru_RU-irina-medium.onnx \
    https://huggingface.co/rhasspy/piper-voices/resolve/main/ru/ru_RU/irina/medium/ru_RU-irina-medium.onnx
  curl -L -o backend/models/ru_RU-irina-medium.onnx.json \
    https://huggingface.co/rhasspy/piper-voices/resolve/main/ru/ru_RU/irina/medium/ru_RU-irina-medium.onnx.json
  ```

## Запуск

```bash
# backend
cd backend
cp .env.example .env   # whisper-cli/модель, Ollama, HA_URL + HA_TOKEN
npm install
npm run dev             # tsx watch server.ts, http://localhost:3001
# или для прода: npm run build && npm start

# frontend (в отдельном терминале)
cd frontend
npm install
npm run dev              # http://localhost:5173, /api проксируется на backend
```

Откройте http://localhost:5173 и пройдите три шага:
1. «🎤 Записать» → «⏹ Остановить» — появится транскрипт, который можно
   отредактировать перед отправкой.
2. «Подтвердить и отправить в LLM →» — LLM возвращает JSON-команду
   (`action`/`entity`/`value`/`response_text`), который тоже можно
   отредактировать (например, исправить entity_id) перед выполнением.
3. «Подтвердить и выполнить в Home Assistant →» — backend вызывает
   соответствующий HA-сервис и показывает `response_text`.

Прошивка ESP32-S3 в `esp32-client/` делает шаг 1 автономно (wake word +
запись на устройстве) и шлёт аудио на тот же `/api/transcribe` — см.
`esp32-client/README.md`. Шаги 2-3 для неё пока не подключены (см. там же).

Прошивка ESP32-P4 в `esp32-p4-wifi6/` проходит все три шага сама — см.
`esp32-p4-wifi6/README.md`.

Для боевого развёртывания всего стека на Raspberry Pi 5 — пошаговая
инструкция в [`DEPLOY-RPI5.md`](DEPLOY-RPI5.md).

## Конфигурация backend (`backend/.env`)

| Переменная | Назначение |
|---|---|
| `WHISPER_BIN` | путь к бинарнику `whisper-cli` |
| `WHISPER_MODEL` | путь к `.bin`-модели whisper |
| `WHISPER_LANGUAGE` | `auto`, `ru`, `en`, ... |
| `FFMPEG_BIN` | путь к `ffmpeg` |
| `PIPER_BIN` | путь к бинарнику `piper` (для `/api/tts`, опционально) |
| `PIPER_MODEL` | путь к голосовой `.onnx`-модели Piper (нужен соседний `.onnx.json`) |
| `OLLAMA_URL` | адрес Ollama, по умолчанию `http://localhost:11434` |
| `OLLAMA_MODEL` | имя модели Ollama, например `llama3` |
| `HA_URL` | адрес Home Assistant, например `http://homeassistant.local:8123` |
| `HA_TOKEN` | Long-Lived Access Token из Home Assistant |
| `SYSTEM_PROMPT_PATH` | путь к файлу system prompt, по умолчанию `../system_prompt` |

## Структура backend

- `server.ts` — bootstrap: создаёт Express app, подключает middleware и `router`, слушает `PORT`.
- `router.ts` — все эндпоинты и их хелперы (whisper/ffmpeg/piper subprocess, запрос к Ollama, вызов HA-сервисов).
- `config.ts` — все переменные окружения (`PORT`, `WHISPER_*`, `PIPER_*`, `OLLAMA_*`, `HA_*`, `SYSTEM_PROMPT_PATH`) в одном месте.
- `types.ts` — общие типы (`HaCommand`, `HaState`).

## API

| Эндпоинт | Вход | Выход |
|---|---|---|
| `POST /api/transcribe` | `multipart/form-data`, поле `audio` | `{ text }` |
| `POST /api/llm-command` | `{ text }` | `{ action, entity, value, response_text }` |
| `POST /api/ha-execute` | `{ action, entity, value, response_text }` | `{ executed, response_text, changed? }` |
| `POST /api/tts` | `{ text }` | `audio/wav` (бинарный ответ) |
