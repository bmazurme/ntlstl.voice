import { Router, type Request, type Response } from 'express';
import multer from 'multer';
import { execFile } from 'node:child_process';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import {
  FFMPEG_BIN,
  HA_ENTITY_DOMAINS,
  HA_TOKEN,
  HA_URL,
  OLLAMA_MODEL,
  OLLAMA_URL,
  SYSTEM_PROMPT_PATH,
  WHISPER_BIN,
  WHISPER_LANGUAGE,
  WHISPER_MODEL,
} from './config.js';
import type { HaCommand, HaState } from './types.js';
import { errorMessage } from './utils.js';

function run(bin: string, args: string[]): Promise<{ stdout: string; stderr: string }> {
  return new Promise((resolve, reject) => {
    execFile(bin, args, { maxBuffer: 1024 * 1024 * 10 }, (error, stdout, stderr) => {
      if (error) {
        reject(new Error(`${path.basename(bin)} failed: ${error.message}\n${stderr}`));
        return;
      }
      resolve({ stdout, stderr });
    });
  });
}

async function fetchHaEntityList(): Promise<string[]> {
  if (!HA_TOKEN) return [];

  const haRes = await fetch(`${HA_URL}/api/states`, {
    headers: { Authorization: `Bearer ${HA_TOKEN}` },
  });
  if (!haRes.ok) return [];

  const states = (await haRes.json()) as HaState[];
  return states
    .filter((s) => HA_ENTITY_DOMAINS.includes(s.entity_id.split('.')[0]))
    .map((s) => `- ${s.attributes?.friendly_name || s.entity_id}: ${s.entity_id}`);
}

const COVER_SERVICE: Record<string, string> = { turn_on: 'open_cover', turn_off: 'close_cover', toggle: 'toggle' };

function resolveHaService(domain: string, action: string): string | null {
  if (domain === 'cover' && COVER_SERVICE[action]) {
    return COVER_SERVICE[action];
  }
  if (action === 'turn_on' || action === 'turn_off' || action === 'toggle') {
    return action;
  }
  if (action === 'set_brightness') {
    return 'turn_on';
  }
  return null;
}

const upload = multer({ storage: multer.memoryStorage(), limits: { fileSize: 25 * 1024 * 1024 } });

const router = Router();

router.post('/api/transcribe', upload.single('audio'), async (req: Request, res: Response) => {
  if (!req.file) {
    return res.status(400).json({ error: 'No audio file uploaded' });
  }

  const workDir = await mkdtemp(path.join(tmpdir(), 'stt-'));
  const inputPath = path.join(workDir, 'input.audio');
  const wavPath = path.join(workDir, 'audio.wav');
  const startedAt = Date.now();

  try {
    await writeFile(inputPath, req.file.buffer);

    // Convert whatever the browser recorded (webm/opus, ogg, etc.) into
    // 16kHz mono WAV, the format whisper.cpp expects.
    await run(FFMPEG_BIN, ['-y', '-i', inputPath, '-ar', '16000', '-ac', '1', wavPath]);

    const { stdout } = await run(WHISPER_BIN, [
      '-m', WHISPER_MODEL,
      '-f', wavPath,
      '-l', WHISPER_LANGUAGE,
      '-np',
      '-nt',
    ]);

    res.json({
      text: stdout.trim(),
      model: path.basename(WHISPER_MODEL),
      duration_ms: Date.now() - startedAt,
    });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: errorMessage(err) });
  } finally {
    await rm(workDir, { recursive: true, force: true });
  }
});

router.post('/api/llm-command', async (req: Request, res: Response) => {
  const { text } = req.body as { text?: string };
  if (!text || !text.trim()) {
    return res.status(400).json({ error: 'No text provided' });
  }

  const startedAt = Date.now();

  try {
    const baseSystemPrompt = await readFile(SYSTEM_PROMPT_PATH, 'utf-8');
    const entities = await fetchHaEntityList();
    const systemPrompt = entities.length
      ? `${baseSystemPrompt}\n\nСУЩНОСТИ HOME ASSISTANT:\n${entities.join('\n')}`
      : baseSystemPrompt;

    const ollamaRes = await fetch(`${OLLAMA_URL}/api/chat`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      // keep_alive is a top-level field, not an "options" entry - keeping it
      // nested (as before) made Ollama silently ignore it, so the model was
      // never actually kept warm. On a Raspberry Pi 5 (CPU-only, slow
      // storage) reloading weights is expensive, so this matters a lot.
      body: JSON.stringify({
        model: OLLAMA_MODEL,
        messages: [
          { role: 'system', content: systemPrompt },
          { role: 'user', content: text },
        ],
        format: 'json',
        stream: false,
        keep_alive: '30m',
        options: {
          temperature: 0.05,
          num_predict: 128,
          // Pi 5 is a quad-core Cortex-A76 with no usable GPU offload for
          // Ollama, so inference is CPU-bound: pin all 4 cores, and keep
          // the context window just large enough for our prompt + the
          // injected entity list to avoid over-allocating KV-cache.
          num_thread: 4,
          num_ctx: 1024,
        },
      }),
      // The Pi can take a while under load (cold model load can exceed 60s
      // for phi3); fail fast with a clear error instead of hanging forever.
      signal: AbortSignal.timeout(120_000),
    });

    if (!ollamaRes.ok) {
      const errText = await ollamaRes.text();
      return res.status(502).json({ error: `Ollama error: ${errText}` });
    }

    const data = (await ollamaRes.json()) as { message?: { content?: string } };
    const raw = data?.message?.content || '';

    let command: HaCommand;
    try {
      command = JSON.parse(raw);
    } catch {
      const match = raw.match(/\{[\s\S]*\}/);
      if (!match) throw new Error(`LLM did not return JSON: ${raw}`);
      command = JSON.parse(match[0]);
    }

    res.json({ command, model: OLLAMA_MODEL, duration_ms: Date.now() - startedAt });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: errorMessage(err) });
  }
});

router.post('/api/ha-execute', async (req: Request, res: Response) => {
  const { action, entity, value, response_text } = req.body as HaCommand;

  if (!action || action === 'query' || action === 'unknown' || !entity) {
    return res.json({ executed: false, response_text: response_text || '', duration_ms: 0 });
  }
  if (!HA_TOKEN) {
    return res.status(500).json({ error: 'HA_TOKEN is not configured on the backend (.env)' });
  }

  const domain = entity.split('.')[0];
  const service = resolveHaService(domain, action);
  if (!service) {
    return res.status(400).json({ error: `Unknown action: ${action}` });
  }

  const body: { entity_id: string; brightness_pct?: number } = { entity_id: entity };
  if (action === 'set_brightness' && value != null) {
    body.brightness_pct = value;
  }

  const startedAt = Date.now();

  try {
    const haRes = await fetch(`${HA_URL}/api/services/${domain}/${service}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        Authorization: `Bearer ${HA_TOKEN}`,
      },
      body: JSON.stringify(body),
    });

    if (!haRes.ok) {
      const errText = await haRes.text();
      return res.status(502).json({ error: `Home Assistant error: ${errText}` });
    }

    const changed = await haRes.json();
    res.json({ executed: true, response_text: response_text || '', changed, duration_ms: Date.now() - startedAt });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: errorMessage(err) });
  }
});

export default router;
