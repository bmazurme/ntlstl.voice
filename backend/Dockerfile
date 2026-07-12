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

WORKDIR /app
COPY backend/package.json backend/package-lock.json ./
RUN npm ci --omit=dev
COPY --from=backend-builder /app/dist ./dist
COPY system_prompt /app/system_prompt

ENV WHISPER_BIN=/usr/local/bin/whisper-cli
ENV FFMPEG_BIN=/usr/bin/ffmpeg
ENV WHISPER_MODEL=/app/models/ggml-small.bin
ENV SYSTEM_PROMPT_PATH=/app/system_prompt
ENV PORT=3001

EXPOSE 3001
CMD ["node", "dist/server.js"]
