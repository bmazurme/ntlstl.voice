# Голосовой клиент для Waveshare ESP32-P4-WIFI6

Прошивка (ESP-IDF) для платы [Waveshare **ESP32-P4-WIFI6**](https://docs.waveshare.com/ESP32-P4-WIFI6).
Услышав wake word («Hi, ESP» по умолчанию), записывает несколько секунд с
онбортового микрофона и сама проходит весь голосовой конвейер бэкенда
(см. `../backend`): транскрибирует клип, превращает текст в команду через
LLM и выполняет её в Home Assistant — без участия фронтенда/человека.

```
[микрофон ES8311] → WakeNet (on-device) → запись N секунд → WAV
   → POST /api/transcribe   (whisper.cpp)         → текст
   → POST /api/llm-command  (Ollama)               → { action, entity, value, response_text }
   → POST /api/ha-execute   (Home Assistant)        → выполнено
   → POST /api/tts          (Piper, best-effort)    → WAV → колонка ES8311/NS4150B
```

В отличие от `../esp32-client` (ESP32-S3), который останавливается после
`/api/transcribe` и требует дальнейшего доведения через React-фронтенд,
эта прошивка проходит все три шага бэкенда сама (`main/backend_client.c`).
Если на каком-то шаге бэкенд вернул ошибку — конвейер для этого клипа
просто останавливается, следующий wake word запустит новую попытку с нуля.

Аудио никуда не отправляется, пока не сработает wake word — само
распознавание wake word работает целиком на устройстве (esp-sr WakeNet),
без сети.

## Особенности этой платы

- **WiFi 6 — не в самом P4.** У ESP32-P4 нет собственного радио; WiFi 6
  обеспечивает **onboard-сопроцессор ESP32-C6** по каналу `esp-hosted`
  (SDIO). В прошивке это прозрачно: компоненты `esp_wifi_remote` /
  `esp_hosted` перенаправляют обычные вызовы `esp_wifi_*` на C6, поэтому
  `wifi.c` выглядит как для обычного ESP32. C6 приходит с уже прошитой
  slave-прошивкой с завода.
- **Микрофон — через кодек ES8311** (I2C-адрес `0x18`) + усилитель
  NS4150B, а не «голый» I2S-микрофон. Кодек настраивается по I2C
  (компонент `esp_codec_dev`), после чего звук читается по I2S. Внешний
  микрофон подключать не нужно.
- **Wake word — офлайн, через esp-sr v2.** WakeNet поддерживает ESP32-P4
  начиная с esp-sr v2.x; модель wake word пакуется при сборке в
  `build/srmodels/srmodels.bin` и прошивается в отдельный раздел `model`
  (см. `partitions.csv`). Фраза по умолчанию — **«Hi, ESP»**
  (`CONFIG_SR_WN_WN9_HIESP`); другую (Alexa, Jarvis, ...) можно выбрать в
  `menuconfig` → `ESP Speech Recognition` → `Wake Word Engine`, затем
  пересобрать и перепрошить.

## Распиновка (значения по умолчанию)

Все пины настраиваются через `idf.py menuconfig` → «P4 Voice Client
Configuration». Значения по умолчанию соответствуют документации Waveshare:

| Сигнал | GPIO | Пункт Kconfig |
|---|---|---|
| I2S MCLK | 13 | `P4_I2S_MCLK_GPIO` |
| I2S BCLK (SCLK) | 12 | `P4_I2S_BCLK_GPIO` |
| I2S WS (LRCLK) | 10 | `P4_I2S_WS_GPIO` |
| I2S DIN (микрофон, ASDOUT кодека) | 11 | `P4_I2S_DIN_GPIO` |
| I2C SDA (управление ES8311) | 7 | `P4_I2C_SDA_GPIO` |
| I2C SCL (управление ES8311) | 8 | `P4_I2C_SCL_GPIO` |
| I2S DOUT (динамик, DSDIN кодека) | 9 | `P4_I2S_DOUT_GPIO` |
| Enable усилителя NS4150B (пин CTRL, через R105) | 53 | `P4_PA_ENABLE_GPIO` |

> ⚠️ Пины желательно сверить со схемой вашей ревизии платы — у разных
> вариантов Waveshare P4 они отличаются. **DOUT и PA enable в публичной
> документации Waveshare для ESP32-P4-WIFI6 не указаны** — значения 9 и 53
> взяты из schematic PDF родственного варианта платы
> (ESP32-P4-WIFI6-Touch-LCD-XC, использует тот же модуль
> ESP32-P4-WIFI6-M) и перепроверены тем, что остальные 4 сигнала в этой
> схеме (MCLK/SCLK/LRCK/ASDOUT = GPIO13/12/10/11) **точно совпали** с уже
> подтверждённо рабочими значениями в этом проекте — но **не проверены на
> реальном железе для звука**. Если не заработает — см.
> [«Озвучка ответа (TTS)»](#озвучка-ответа-tts).

## Запуск на локальной машине

### 1. Поднять бэкенд через docker-compose

Устройство отправляет аудио на бэкенд, поэтому он должен быть запущен и
доступен в вашей локальной сети. Из **корня репозитория**:

```bash
cp .env.example .env      # заполните HA_TOKEN (см. корневой README)
docker compose up --build
```

Что важно понимать про сеть в `docker-compose.yml`:

- Порт наружу пробрасывает **только `frontend`** (nginx) — `80:80`.
  Сам `backend` порт `3001` наружу **не публикует**.
- Nginx фронтенда проксирует `/api/` → `http://backend:3001/api/`
  (см. `frontend/nginx.conf`).

Поэтому устройство должно ходить на бэкенд **через фронтенд, на порт 80**:

```
http://<IP-хоста-с-docker>/api/transcribe        ← так (порт 80, через nginx)
http://<IP-хоста-с-docker>:3001/api/transcribe   ← так НЕ работает в docker-compose
```

Именно это значение и стоит по умолчанию в `P4_BACKEND_URL`
(`http://192.168.1.100/api/transcribe`) — поменяйте IP на адрес вашего
хоста. Узнать IP: `ipconfig getifaddr en0` (macOS) или `hostname -I`
(Linux). Не используйте `localhost` и `*.local` — устройство их не
разрешит.

Бэкенду также нужны `ffmpeg`, `whisper` и модель + запущенная Ollama —
всё это и переменные окружения описаны в корневом
[`README.md`](../README.md); в docker-compose они уже прописаны в
`docker-compose.yml`.

Быстрая проверка доступности стека из сети (с другого компьютера):

```bash
curl -F "audio=@some.wav" http://<IP-хоста>/api/transcribe
```

### 2. Установить ESP-IDF (v5.3+)

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.3.2 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32p4
. ./export.sh   # выполнять в каждом новом терминале перед idf.py
```

### 3. Собрать, настроить и прошить

Из каталога `esp32-p4-wifi6/`:

```bash
idf.py set-target esp32p4
idf.py menuconfig   # "P4 Voice Client Configuration" → WiFi SSID/пароль,
                    # P4_BACKEND_URL (IP вашего docker-хоста), при необходимости пины
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Первая сборка сама подтянет через IDF Component Manager компоненты
`esp_wifi_remote`, `esp_hosted`, `esp_codec_dev` и `esp-sr` (см.
`main/idf_component.yml`) и упакует выбранную модель wake word в
`build/srmodels/srmodels.bin`; `idf.py flash` запишет её в раздел
`model` из `partitions.csv`.

В мониторе (`115200` бод) после старта появится подключение к WiFi и
`Starting audio pipeline (say the wake word to record)...`. Скажите
**«Hi, ESP»**, затем команду — прошивка запишет клип
(`P4_RECORD_SECONDS`, по умолчанию 4 с) и пройдёт весь конвейер, в логе
будет видно каждый шаг:

```
I (...) audio_pipeline: Wake word detected, recording 4 s...
I (...) audio_pipeline: Recording complete, uploading...
I (...) backend_client: POST http://<host>/api/transcribe -> HTTP 200
I (...) backend_client: Transcript: Включи свет на кухне.
I (...) backend_client: POST http://<host>/api/llm-command -> HTTP 200
I (...) backend_client: Command: {"action":"turn_on","entity":"switch.wall_switch",...}
I (...) backend_client: POST http://<host>/api/ha-execute -> HTTP 200
I (...) backend_client: ha-execute response: {"executed":true,...}
```

Пока клип загружается и обрабатывается, новые wake word игнорируются.
Команда сработает только если сказанная сущность реально существует в
Home Assistant — LLM не выдумывает `entity_id`, а подставляет только те,
что реально пришли из `GET {HA_URL}/api/states` (см. `../backend/router.ts`).

## Чувствительность wake word

Если wake word стабильно ловится с записи/TTS из динамика, но живой голос
проходит через раз и требует говорить громко — дело в двух настройках:
порог детекции и усиление микрофона.

### Порог детекции (`wakenet_mode`)

Задаётся в `main/audio_pipeline.c`. Это **порог вероятности**, при котором
WakeNet считает, что услышал фразу:

| Значение | Порог | Поведение |
|---|---|---|
| `DET_MODE_90` | ≥ 0.90 | **чувствительнее** — ловит живой голос обычной громкости, чуть больше ложных срабатываний |
| `DET_MODE_95` | ≥ 0.95 | строже — требует громкого/чистого сигнала, живой голос часто пропускает |

По умолчанию стоит `DET_MODE_90`. Громкий ровный TTS из динамика даёт
уверенность ~0.97+ и проходит любой порог; живой голос обычно 0.90–0.94,
поэтому со строгим `DET_MODE_95` он и терялся. Ложное срабатывание здесь
не опасно — оно лишь запускает запись, а не необратимое действие, поэтому
более чувствительный режим предпочтителен:

```c
afe_config->wakenet_mode = DET_MODE_90;   // audio_pipeline.c
```

### Усиление микрофона (`P4_MIC_GAIN_DB`)

Если и с `DET_MODE_90` тихая речь (с 1–1.5 м) теряется — поднимите
усиление ADC ES8311 через `idf.py menuconfig` → «P4 Voice Client
Configuration» → «Microphone input gain (dB)» (по умолчанию 30 дБ):

```
30 → 36 → 40 …   поднимайте по шагам
```

> ⚠️ Слишком большое усиление даёт **клиппинг** на громких звуках, а
> искажённый сигнал WakeNet распознаёт *хуже*, чем тихий. Останавливайтесь,
> как только живой голос начинает срабатывать стабильно. Признак клиппинга —
> хрип в записи и «сыпется» транскрипция на громких фразах; тогда откатите
> gain на шаг назад.

Порядок подбора: сперва `DET_MODE_90` (уже стоит), затем при необходимости
gain 30→36→40, и только потом — физика (микрофон маленький и без
направленности: говорить в сторону платы, без сильного шума/эха).

## Озвучка ответа (TTS)

После `ha-execute` прошивка запрашивает у бэкенда `POST /api/tts { text:
response_text }` (локальный [Piper](https://github.com/rhasspy/piper), см.
корневой [`README.md`](../README.md#конфигурация-backend-backendenv)) и
проигрывает полученный WAV через встроенную колонку — этот шаг
best-effort: если TTS недоступен или не настроен, HA-команда всё равно уже
выполнилась, устройство просто останется немым для этого клипа.

Для проигрывания нужен **второй, не документированный Waveshare**
I2S-сигнал — DOUT (данные на DSDIN кодека ES8311). По умолчанию
`P4_I2S_DOUT_GPIO = 9` и `P4_PA_ENABLE_GPIO = 53` — значения найдены в
schematic PDF родственной платы (см. [«Распиновка»](#распиновка-значения-по-умолчанию)
выше), **но не проверены на реальном звуке**. Если после прошивки динамик
молчит или шумит:

1. Сверьте 9/53 со схемой именно вашей ревизии платы, если она доступна.
2. `idf.py menuconfig` → «P4 Voice Client Configuration» → поправьте «I2S
   DOUT» / «Speaker amplifier enable GPIO» на верные номера (или `-1`,
   чтобы временно выключить TTS и убедиться, что остальной пайплайн не
   пострадал — при `-1` `audio_pipeline_play()` просто залогирует
   предупреждение и пропустит проигрывание).
3. Пересоберите и перепрошейте.

Если после этого звук всё ещё не идёт, а по логам `esp_codec_dev_write`
отрабатывает без ошибок — на плате может быть отдельный GPIO,
включающий усилитель NS4150B (его `CTRL`-пин); задайте его в «Speaker
amplifier (NS4150B) enable GPIO». Многие платы вместо этого держат `CTRL`
всегда притянутым к питанию — тогда значение по умолчанию (`-1`, пин не
трогается) уже корректно.

Громкость выставляется через «Speaker output volume (0-100)»
(`P4_SPEAKER_VOLUME`, по умолчанию 70).

## Диагностика

- **WiFi не подключается** — убедитесь, что C6 несёт валидную
  `esp-hosted` slave-прошивку и что установлены компоненты
  `esp_wifi_remote`/`esp_hosted`. Проверьте SSID/пароль в menuconfig.
- **`Failed to open connection` / connection refused** — бэкенд
  недоступен: неверный IP в `P4_BACKEND_URL`, или указан порт `:3001`
  вместо `80` (в docker-compose наружу торчит только nginx на `:80`), или
  фаервол хоста блокирует входящие. Сначала проверьте `curl`-ом выше.
- **`No wake word model found in the 'model' partition`** — модель не
  прошита: используйте `idf.py flash` (а не только `app-flash`), чтобы
  вместе с приложением записался и раздел `model`.
- **Wake word не срабатывает / тишина** — сверьте пины I2S/I2C и адрес
  ES8311 со схемой платы. Если аудио идёт, но фраза ловится плохо или
  только на громкости — см. раздел [«Чувствительность wake word»](#чувствительность-wake-word)
  (порог `DET_MODE_90/95` и `P4_MIC_GAIN_DB`).
- **Ошибка PSRAM при загрузке / out of memory** — плата несёт 32 МБ
  hex-режимного PSRAM (`CONFIG_SPIRAM_MODE_HEX` в `sdkconfig.defaults`);
  если ваша ревизия другая, поменяйте режим. PSRAM обязателен: из него
  выделяются буферы AFE/WakeNet и буфер записи.
- **`TTS playback requested but P4_I2S_DOUT_GPIO is not configured,
  skipping`** — ожидаемо, пока не заданы пины динамика, см.
  [«Озвучка ответа (TTS)»](#озвучка-ответа-tts).
- **Динамик молчит, хотя `P4_I2S_DOUT_GPIO` задан и `esp_codec_dev_write`
  не логирует ошибок** — вероятно, нужен отдельный GPIO для
  `P4_PA_ENABLE_GPIO` (усилитель NS4150B не включается сам), либо DOUT
  подключён не туда — сверьте со схемой платы.
- **`piper failed` / `HTTP 500` от `/api/tts` на бэкенде** — `PIPER_BIN`
  или `PIPER_MODEL` не настроены/не найдены на бэкенде (см. корневой
  [`README.md`](../README.md)); шаг TTS best-effort, HA-команда до этого
  уже выполнилась.
