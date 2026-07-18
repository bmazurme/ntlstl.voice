import { createSlice } from '@reduxjs/toolkit';
import type { PayloadAction } from '@reduxjs/toolkit';
import type { HaExecuteResult } from '../types.ts';

export type Stage = 'idle' | 'transcribed' | 'commanded' | 'executed';
export type Busy = 'transcribing' | 'thinking' | 'executing' | null;
export type StepMeta = { model: string; duration_ms: number } | null;

export interface PipelineState {
  stage: Stage;
  busy: Busy;
  transcript: string;
  transcribeMeta: StepMeta;
  commandText: string;
  llmMeta: StepMeta;
  haResult: HaExecuteResult | null;
  error: string;
  autoMode: boolean;
  countdown: number | null;
}

const initialState: PipelineState = {
  stage: 'idle',
  busy: null,
  transcript: '',
  transcribeMeta: null,
  commandText: '',
  llmMeta: null,
  haResult: null,
  error: '',
  autoMode: false,
  countdown: null,
};

const pipelineSlice = createSlice({
  name: 'pipeline',
  initialState,
  reducers: {
    setBusy(state, action: PayloadAction<Busy>) {
      state.busy = action.payload;
    },
    setError(state, action: PayloadAction<string>) {
      state.error = action.payload;
    },
    setTranscript(state, action: PayloadAction<string>) {
      state.transcript = action.payload;
    },
    setCommandText(state, action: PayloadAction<string>) {
      state.commandText = action.payload;
    },
    setAutoMode(state, action: PayloadAction<boolean>) {
      state.autoMode = action.payload;
    },
    setCountdown(state, action: PayloadAction<number | null>) {
      state.countdown = action.payload;
    },
    tickCountdown(state) {
      if (state.countdown !== null) state.countdown -= 1;
    },
    // A new recording resets everything downstream back to idle.
    startRecording(state) {
      state.stage = 'idle';
      state.transcript = '';
      state.transcribeMeta = null;
      state.commandText = '';
      state.llmMeta = null;
      state.haResult = null;
      state.error = '';
    },
    transcribed(
      state,
      action: PayloadAction<{ text: string; meta: StepMeta }>,
    ) {
      state.transcript = action.payload.text;
      state.transcribeMeta = action.payload.meta;
      state.commandText = '';
      state.llmMeta = null;
      state.haResult = null;
      state.stage = 'transcribed';
    },
    commanded(
      state,
      action: PayloadAction<{ commandText: string; meta: StepMeta }>,
    ) {
      state.commandText = action.payload.commandText;
      state.llmMeta = action.payload.meta;
      state.stage = 'commanded';
    },
    executed(state, action: PayloadAction<HaExecuteResult>) {
      state.haResult = action.payload;
      state.stage = 'executed';
    },
    reset() {
      return initialState;
    },
  },
});

export const {
  setBusy,
  setError,
  setTranscript,
  setCommandText,
  setAutoMode,
  setCountdown,
  tickCountdown,
  startRecording,
  transcribed,
  commanded,
  executed,
  reset,
} = pipelineSlice.actions;

export default pipelineSlice.reducer;
