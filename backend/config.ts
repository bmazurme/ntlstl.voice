import path from 'node:path';

export const PORT = process.env.PORT || 3001;

export const WHISPER_BIN = process.env.WHISPER_BIN || 'whisper-cli';
export const WHISPER_MODEL = process.env.WHISPER_MODEL || './models/ggml-small.bin';
export const FFMPEG_BIN = process.env.FFMPEG_BIN || 'ffmpeg';
export const WHISPER_LANGUAGE = process.env.WHISPER_LANGUAGE || 'auto';

export const OLLAMA_URL = process.env.OLLAMA_URL || 'http://localhost:11434';
export const OLLAMA_MODEL = process.env.OLLAMA_MODEL || 'llama3';

export const HA_URL = process.env.HA_URL || 'http://homeassistant.local:8123';
export const HA_TOKEN = process.env.HA_TOKEN || '';
export const HA_ENTITY_DOMAINS = ['light', 'switch', 'cover', 'fan', 'climate', 'lock', 'media_player'];

export const SYSTEM_PROMPT_PATH = process.env.SYSTEM_PROMPT_PATH || path.join(process.cwd(), '..', 'system_prompt');
