# ---- whisper.cpp (built from source, pinned to the version used in dev) ----
FROM debian:bookworm-slim AS whisper-builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /opt
RUN git clone --depth 1 --branch v1.9.1 https://github.com/ggerganov/whisper.cpp.git
WORKDIR /opt/whisper.cpp
# GGML_NATIVE=OFF: avoids a GCC 12 / ARM NEON fp16 intrinsic incompatibility
# ("target specific option mismatch") seen with -mcpu=native autodetection
# under emulated/virtualized aarch64 (e.g. Docker Desktop's VM).
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DGGML_NATIVE=OFF -DBUILD_SHARED_LIBS=OFF \
    && cmake --build build -j --config Release

# ---- piper (prebuilt binary - it's a small C++ ONNX runtime wrapper, not
# worth building from source) ----
FROM debian:bookworm-slim AS piper-downloader
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /opt
# Repo is archived/read-only as of its last release; pin it explicitly.
ARG PIPER_VERSION=2023.11.14-2
RUN ARCH=$(uname -m) && \
    curl -L -o piper.tar.gz \
      "https://github.com/rhasspy/piper/releases/download/${PIPER_VERSION}/piper_linux_${ARCH}.tar.gz" && \
    tar -xzf piper.tar.gz && rm piper.tar.gz

# ---- backend TypeScript build ----
# Build context is the project root (set in docker-compose.yml).
FROM node:22-bookworm-slim AS backend-builder
WORKDIR /app
COPY backend/package.json backend/package-lock.json ./
RUN npm ci
COPY backend/tsconfig.json ./
COPY backend/*.ts ./
RUN npm run build

# ---- runtime ----
FROM node:22-bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    ffmpeg libgomp1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=whisper-builder /opt/whisper.cpp/build/bin/whisper-cli /usr/local/bin/whisper-cli
COPY --from=piper-downloader /opt/piper /opt/piper

WORKDIR /app
COPY backend/package.json backend/package-lock.json ./
RUN npm ci --omit=dev
COPY --from=backend-builder /app/dist ./dist
COPY system_prompt /app/system_prompt

ENV WHISPER_BIN=/usr/local/bin/whisper-cli
ENV FFMPEG_BIN=/usr/bin/ffmpeg
ENV WHISPER_MODEL=/app/models/ggml-small.bin
ENV PIPER_BIN=/opt/piper/piper
# Voice model isn't baked into the image - like the whisper model, it's
# expected to be downloaded into ./backend/models (see root README) and
# mounted in via the models volume in docker-compose.yml.
ENV PIPER_MODEL=/app/models/ru_RU-irina-medium.onnx
ENV SYSTEM_PROMPT_PATH=/app/system_prompt
ENV PORT=3001

EXPOSE 3001
CMD ["node", "dist/server.js"]
