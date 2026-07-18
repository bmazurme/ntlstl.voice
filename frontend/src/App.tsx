import { useRef } from 'react';
import {
  Alert,
  Button,
  Card,
  Checkbox,
  Spin,
  Text,
  TextArea,
} from '@gravity-ui/uikit';
import { useRecorder } from './useRecorder.ts';
import {
  apiErrorMessage,
  useHaExecuteMutation,
  useLlmCommandMutation,
  useTranscribeMutation,
} from './store/api.ts';
import { useAppDispatch, useAppSelector } from './store/hooks.ts';
import {
  commanded,
  executed,
  reset,
  setAutoMode,
  setBusy,
  setCommandText,
  setCountdown,
  setError,
  setTranscript,
  startRecording,
  tickCountdown,
  transcribed,
} from './store/slice.ts';
import type { StepMeta } from './store/slice.ts';
import type { HaCommand } from './types.ts';
import { errorMessage } from './utils.ts';
import './App.css';

// Mirrors esp32-client's CONFIG_STT_RECORD_SECONDS default: on the device,
// recording starts on wake word and always runs for a fixed duration, no
// manual stop. The web UI follows the same shape (click = "wake word"
// trigger, then a fixed-length recording) so behavior stays comparable.
const RECORD_SECONDS = 4;

function MetaLine({ meta }: { meta: StepMeta }) {
  if (!meta) return null;
  return (
    <Text color="secondary" variant="caption-2" className="meta">
      Модель: <code>{meta.model}</code> · {meta.duration_ms} мс
    </Text>
  );
}

function App() {
  const { isRecording, start, stop } = useRecorder();
  const dispatch = useAppDispatch();
  const {
    stage,
    busy,
    transcript,
    transcribeMeta,
    commandText,
    llmMeta,
    haResult,
    error,
    autoMode,
    countdown,
  } = useAppSelector((s) => s.pipeline);

  const [transcribeAudio] = useTranscribeMutation();
  const [getLlmCommand] = useLlmCommandMutation();
  const [executeHaCommand] = useHaExecuteMutation();

  const autoStopTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const countdownIntervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  async function runLLMStep(text: string) {
    dispatch(setError(''));
    dispatch(setBusy('thinking'));
    try {
      const { command, model, duration_ms } = await getLlmCommand(text).unwrap();
      dispatch(
        commanded({
          commandText: JSON.stringify(command, null, 2),
          meta: { model, duration_ms },
        }),
      );
      if (autoMode) {
        await runHAStep(command);
      }
    } catch (err) {
      dispatch(setError(apiErrorMessage(err, 'LLM command request failed')));
    } finally {
      dispatch(setBusy(null));
    }
  }

  async function runHAStep(command: HaCommand) {
    dispatch(setError(''));
    dispatch(setBusy('executing'));
    try {
      const result = await executeHaCommand(command).unwrap();
      dispatch(executed(result));
    } catch (err) {
      dispatch(setError(apiErrorMessage(err, 'Home Assistant request failed')));
    } finally {
      dispatch(setBusy(null));
    }
  }

  function clearRecordingTimers() {
    if (autoStopTimerRef.current) clearTimeout(autoStopTimerRef.current);
    if (countdownIntervalRef.current) clearInterval(countdownIntervalRef.current);
    autoStopTimerRef.current = null;
    countdownIntervalRef.current = null;
  }

  async function stopAndTranscribe() {
    clearRecordingTimers();
    dispatch(setCountdown(null));
    dispatch(setBusy('transcribing'));
    const blob = await stop();
    try {
      if (!blob) {
        throw new Error('Recording produced no audio data');
      }
      const { text, model, duration_ms } = await transcribeAudio(blob).unwrap();
      dispatch(transcribed({ text, meta: { model, duration_ms } }));
      if (autoMode) {
        await runLLMStep(text);
      }
    } catch (err) {
      dispatch(setError(apiErrorMessage(err, errorMessage(err))));
    } finally {
      dispatch(setBusy(null));
    }
  }

  async function handleRecordClick() {
    dispatch(setError(''));
    if (isRecording) {
      await stopAndTranscribe();
    } else {
      dispatch(startRecording());
      try {
        await start();
        dispatch(setCountdown(RECORD_SECONDS));
        countdownIntervalRef.current = setInterval(() => {
          dispatch(tickCountdown());
        }, 1000);
        autoStopTimerRef.current = setTimeout(stopAndTranscribe, RECORD_SECONDS * 1000);
      } catch (err) {
        dispatch(setError('Не удалось получить доступ к микрофону: ' + errorMessage(err)));
      }
    }
  }

  async function handleConfirmToLLM() {
    await runLLMStep(transcript);
  }

  async function handleConfirmToHA() {
    let command: HaCommand;
    try {
      command = JSON.parse(commandText);
    } catch {
      dispatch(setError('Команда не является валидным JSON — исправьте перед отправкой'));
      return;
    }
    await runHAStep(command);
  }

  function handleReset() {
    clearRecordingTimers();
    dispatch(reset());
  }

  return (
    <div className="app">
      <Text as="h1" variant="display-2" className="title">
        ESP32 → STT → LLM → HA
      </Text>
      <Text as="p" color="secondary" className="subtitle">
        Пошаговая отладка: запись → транскрипт → команда (JSON) → Home Assistant
      </Text>

      <Checkbox
        size="l"
        checked={autoMode}
        onUpdate={(checked) => dispatch(setAutoMode(checked))}
        className="auto-toggle"
      >
        Выполнять всю цепочку автоматически (без подтверждений)
      </Checkbox>

      {error && (
        <Alert theme="danger" message={error} className="alert" />
      )}

      <Card view="outlined" className="panel">
        <Text as="h2" variant="subheader-2">
          1. Запись и распознавание (Whisper.cpp)
        </Text>
        <Button
          size="xl"
          view={isRecording ? 'outlined-danger' : 'action'}
          pin="circle-circle"
          onClick={handleRecordClick}
          loading={busy === 'transcribing'}
          disabled={busy !== null}
          className="record-btn"
        >
          {isRecording ? '⏹ Остановить' : '🎤 Записать'}
        </Button>
        <Text as="p" color="secondary" className="status">
          {isRecording && countdown !== null && `Идёт запись… остановится через ${countdown}с`}
          {busy === 'transcribing' && 'Распознаю речь…'}
        </Text>
        <TextArea
          value={transcript}
          onUpdate={(value) => dispatch(setTranscript(value))}
          placeholder="Здесь появится распознанный текст..."
          rows={3}
        />
        <MetaLine meta={transcribeMeta} />
        <Button
          view="normal"
          onClick={handleConfirmToLLM}
          disabled={!transcript.trim() || busy !== null || autoMode}
        >
          Подтвердить и отправить в LLM →
        </Button>
      </Card>

      <Card
        view="outlined"
        className={`panel ${stage === 'idle' ? 'panel-disabled' : ''}`}
      >
        <Text as="h2" variant="subheader-2">
          2. Команда от LLM (JSON)
        </Text>
        <Text as="p" color="secondary" className="status">
          {busy === 'thinking' && (
            <>
              <Spin size="xs" /> LLM формирует команду…
            </>
          )}
        </Text>
        <TextArea
          value={commandText}
          onUpdate={(value) => dispatch(setCommandText(value))}
          placeholder='{"action": "...", "entity": "...", "value": null, "response_text": "..."}'
          rows={7}
          disabled={stage === 'idle'}
          className="command-json"
        />
        <MetaLine meta={llmMeta} />
        <Button
          view="normal"
          onClick={handleConfirmToHA}
          disabled={stage === 'idle' || !commandText.trim() || busy !== null || autoMode}
        >
          Подтвердить и выполнить в Home Assistant →
        </Button>
      </Card>

      <Card
        view="outlined"
        className={`panel ${stage !== 'executed' && busy !== 'executing' ? 'panel-disabled' : ''}`}
      >
        <Text as="h2" variant="subheader-2">
          3. Выполнение в Home Assistant
        </Text>
        <Text as="p" color="secondary" className="status">
          {busy === 'executing' && (
            <>
              <Spin size="xs" /> Home Assistant выполняет команду…
            </>
          )}
        </Text>
        <div className="reply">
          {haResult ? (
            <>
              <div>{haResult.response_text || '(нет голосового ответа)'}</div>
              <Text color="secondary" variant="caption-2" className="ha-meta">
                executed: {String(haResult.executed)} · {haResult.duration_ms} мс
              </Text>
            </>
          ) : (
            '—'
          )}
        </div>
        {stage === 'executed' && (
          <Button view="normal" onClick={handleReset}>
            Начать заново
          </Button>
        )}
      </Card>
    </div>
  );
}

export default App;
