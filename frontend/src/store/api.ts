import { createApi, fetchBaseQuery } from '@reduxjs/toolkit/query/react';
import type { BaseQueryFn } from '@reduxjs/toolkit/query/react';
import type {
  HaCommand,
  HaExecuteResult,
  LlmCommandResult,
  TranscribeResult,
} from '../types.ts';

// The backend answers errors as { error: string }. fetchBaseQuery surfaces the
// parsed JSON body under error.data, so we normalize it to a plain message.
export function apiErrorMessage(err: unknown, fallback = 'Request failed'): string {
  if (err && typeof err === 'object') {
    const data = (err as { data?: unknown }).data;
    if (data && typeof data === 'object' && 'error' in data) {
      const msg = (data as { error?: unknown }).error;
      if (typeof msg === 'string' && msg) return msg;
    }
    if ('error' in err && typeof (err as { error?: unknown }).error === 'string') {
      return (err as { error: string }).error;
    }
  }
  return fallback;
}

const rawBaseQuery = fetchBaseQuery({ baseUrl: '/api' });

// fetchBaseQuery returns { error: { data: { error } } } on failures; we keep it
// as-is so apiErrorMessage can unwrap it downstream.
const baseQuery: BaseQueryFn = (args, api, extraOptions) =>
  rawBaseQuery(args, api, extraOptions);

export const api = createApi({
  reducerPath: 'api',
  baseQuery,
  endpoints: (builder) => ({
    transcribe: builder.mutation<TranscribeResult, Blob>({
      query: (blob) => {
        const formData = new FormData();
        formData.append('audio', blob, 'recording.webm');
        return { url: '/transcribe', method: 'POST', body: formData };
      },
    }),
    llmCommand: builder.mutation<LlmCommandResult, string>({
      query: (text) => ({
        url: '/llm-command',
        method: 'POST',
        body: { text },
      }),
    }),
    haExecute: builder.mutation<HaExecuteResult, HaCommand>({
      query: (command) => ({
        url: '/ha-execute',
        method: 'POST',
        body: command,
      }),
    }),
  }),
});

export const {
  useTranscribeMutation,
  useLlmCommandMutation,
  useHaExecuteMutation,
} = api;
