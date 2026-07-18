# Развёртывание на Raspberry Pi 5 (192.168.50.240)

Пошаговая инструкция для боевого (production) развёртывания стека
`ESP32 → STT → LLM → HA` (см. корневой [`README.md`](README.md) для общей
архитектуры) на Raspberry Pi 5 в локальной сети, IP `192.168.50.240`.

Целевая машина в этом случае — **Ubuntu 25.04 (aarch64)** на Raspberry Pi
5, подключена по **WiFi (`wlan0`)**, а не Ethernet. Сеть на Ubuntu
настраивается через **Netplan** (не `raspi-config`/`dhcpcd`, как на
классической Raspberry Pi OS) — см. шаг 1.

В отличие от разработки на macOS, здесь **не будет** проблемы с
недоступностью LAN из Docker-контейнеров — это специфика виртуализации
Docker Desktop на macOS. На нативном Linux контейнеры Docker Engine видят
локальную сеть напрямую через bridge-networking, без дополнительных
плясок с портами.

## Что нужно заранее

- Raspberry Pi 5 (рекомендуется 8 ГБ ОЗУ — Ollama и whisper.cpp съедают
  память; на 4 ГБ будет тесно с моделями крупнее `gemma3:1b`).
- **Качественный блок питания на 5 В / 5 А** (27 Вт, официальный USB-C PD
  для Pi 5). Слабый БП режет питание периферии и может давать нестабильную
  работу под нагрузкой (компиляция whisper.cpp, инференс Ollama) — если
  при загрузке видите предупреждение `This power supply is not capable of
  supplying 5A`, замените блок питания раньше, чем разбираться со
  случайными зависаниями.
- Ubuntu **64-bit** (aarch64) — на этой машине уже так.
- Доступ по SSH, обычный пользователь с `sudo`.
- Home Assistant уже работает в той же сети, и есть Long-Lived Access
  Token (Профиль → Безопасность → Долгосрочные токены доступа внизу
  страницы).

## 1. Зафиксировать IP 192.168.50.240 за Pi

DHCP уже выдал этой машине именно `192.168.50.240` (см. `IPv4 address for
wlan0` при логине) — но по DHCP-аренде адрес может смениться после
перевыдачи. Есть два способа его закрепить, и для WiFi-подключения
безопаснее первый:

**Вариант A (рекомендуется) — DHCP-резервация на роутере.** Зайдите в
админку роутера, найдите MAC-адрес `wlan0` (`ip link show wlan0`) и
привяжите к нему `192.168.50.240` в настройках DHCP-резерваций. Ничего на
самой Pi менять не нужно, и нет риска потерять WiFi-подключение из-за
опечатки в конфиге.

**Вариант B — статический IP через Netplan прямо на Pi.** Рискованнее по
SSH: одна ошибка в конфиге WiFi может оборвать соединение и потребовать
физический доступ к устройству (монитор+клавиатура или карта). Если всё
же нужен этот способ:

```bash
ls /etc/netplan/            # найти существующий *.yaml (обычно 50-cloud-init.yaml)
sudo cp /etc/netplan/50-cloud-init.yaml /etc/netplan/50-cloud-init.yaml.bak
sudo nano /etc/netplan/50-cloud-init.yaml
```

Приведите секцию `wifis` к виду (сохранив ваш существующий `access-points`
блок с SSID/паролем — не удаляйте его):

```yaml
network:
  version: 2
  wifis:
    wlan0:
      dhcp4: no
      addresses: [192.168.50.240/24]
      routes:
        - to: default
          via: 192.168.50.1
      nameservers:
        addresses: [192.168.50.1]
      access-points:
        "ВАШ_SSID":
          password: "ВАШ_ПАРОЛЬ"
```

```bash
sudo netplan try   # покажет превью и откатится через 120с, если что-то не так
sudo netplan apply
```

`netplan try` — специально для этого случая: если конфиг оборвёт
соединение, он сам откатится и вы не потеряете доступ по SSH.

## 2. Docker Engine

```bash
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker $USER
newgrp docker   # или перелогиньтесь по SSH
sudo systemctl enable --now docker
docker compose version   # проверка, что compose-плагин на месте
```

## 3. Ollama

```bash
curl -fsSL https://ollama.com/install.sh | sh
sudo systemctl enable --now ollama
ollama pull gemma3:1b
```

`docker-compose.yml` в этом репозитории по умолчанию настроен на
`gemma3:1b` — маленькая (≈815 МБ) и быстрая модель, разумный выбор для
CPU-only Pi 5. Она регулярно ошибается в точном `entity_id` из списка
сущностей Home Assistant (см. `main` README/историю — например путает
`switch.printer` с `printer.printer`), поэтому вызов `ha-execute` может
иногда падать с `400 Bad Request`. Если точность важнее скорости —
`ollama pull llama3.2:latest` (≈2 ГБ, ощутимо точнее с `entity_id`) и
поменяйте `OLLAMA_MODEL: gemma3:1b` на `OLLAMA_MODEL: llama3.2:latest` в
`docker-compose.yml` перед сборкой.

## 4. Клонировать репозиторий

```bash
git clone https://github.com/bmazurme/home.git
cd home
```

## 5. Модель whisper.cpp

Бэкенд не пакует модель в образ (монтируется как volume) — скачайте её
на хост заранее:

```bash
mkdir -p backend/models
curl -L -o backend/models/ggml-small.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin
```

(~487 МБ; для более быстрой, но менее точной транскрипции подойдёт
`ggml-base.bin` — поменяйте `WHISPER_MODEL` в `docker-compose.yml`
соответственно).

## 6. Конфигурация: `docker-compose.yml` и `HA_TOKEN`

`docker-compose.yml` не хранится в публичном репозитории (в нём реальный
LAN IP Home Assistant) — соберите его из примера:

```bash
cp docker-compose.yml.example docker-compose.yml
nano docker-compose.yml   # HA_URL: подставить реальный IP вашего Home Assistant
```

`.local`-имена (`homeassistant.local`) внутри Docker-контейнера **не
резолвятся** ни на macOS, ни на Linux — используйте реальный IP, не mDNS.

Плюс `HA_TOKEN`:

```bash
cp .env.example .env
nano .env   # вставить HA_TOKEN
```

## 7. Собрать и запустить

```bash
docker compose up -d --build
docker compose logs -f backend   # Ctrl+C для выхода из логов (контейнер продолжит работать)
```

Первая сборка компилирует whisper.cpp из исходников (см.
`backend/Dockerfile`) — на Pi 5 это может занять 5-10 минут.

## 8. Проверка

С любой другой машины в сети:

```bash
curl -F "audio=@some.wav" http://192.168.50.240/api/transcribe
curl -H "Content-Type: application/json" \
  -d '{"text":"включи свет"}' \
  http://192.168.50.240/api/llm-command
```

Порт наружу пробрасывает только `frontend` (nginx, `:80`) — сам `backend`
(`:3001`) наружу не публикуется. `/api/` на порту 80 проксируется в
`backend:3001` (см. `frontend/nginx.conf`), поэтому все запросы — на
`http://192.168.50.240/api/...`, без `:3001`.

Веб-интерфейс (debug UI): `http://192.168.50.240/`.

## 9. Направить прошивку ESP32-P4 на Pi

В `esp32-p4-wifi6/` (см. его собственный [`README.md`](esp32-p4-wifi6/README.md)):

```bash
idf.py menuconfig   # "P4 Voice Client Configuration" → Backend URL
```

Впишите `http://192.168.50.240/api/transcribe`, пересоберите и
перепрошейте:

```bash
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

## 10. Автозапуск после перезагрузки Pi

Уже настроено из коробки:
- `docker` включён через `systemctl enable --now docker` (шаг 2).
- `ollama` включён через `systemctl enable --now ollama` (шаг 3).
- Оба сервиса в `docker-compose.yml` имеют `restart: unless-stopped` —
  поднимутся сами вместе с Docker после ребута хоста.

Проверить после `sudo reboot`:

```bash
docker compose ps          # backend/frontend "Up"
systemctl status ollama    # active (running)
```

## Диагностика

- **`ConnectTimeoutError` до Home Assistant из контейнера** — проверьте,
  что `HA_URL` в `docker-compose.yml` — это реальный IP, а не
  `.local`-имя, и что Pi физически видит `192.168.50.135` (`ping` с
  самого Pi).
- **Ollama недоступна из контейнера** — `extra_hosts:
  host.docker.internal:host-gateway` требует Docker Engine ≥ 20.10;
  проверьте `docker --version`. Также убедитесь, что `ollama serve`
  слушает не только `127.0.0.1` — по умолчанию Ollama слушает
  `127.0.0.1:11434`, что закрыто для контейнеров; проверьте
  `OLLAMA_HOST=0.0.0.0` в `/etc/systemd/system/ollama.service.d/override.conf`
  при необходимости, либо просто полагайтесь на `host.docker.internal`,
  который штатно резолвится в IP хоста и достаёт `127.0.0.1`-сервисы
  хоста через host-gateway маршрут.
- **`ha-execute` возвращает 400** — LLM подставила несуществующий
  `entity_id`; см. раздел про модели в шаге 3.
- **Сборка `whisper.cpp` падает / очень медленная** — Pi 5 собирает из
  исходников при каждой пересборке образа без изменений в
  `backend/Dockerfile`, слоя Docker-кеша обычно достаточно, чтобы не
  пересобирать whisper.cpp повторно; если кеш сброшен (`--no-cache`),
  ожидайте полные 5-10 минут на этот шаг.
