import { configureStore } from '@reduxjs/toolkit';
import { api } from './api.ts';
import pipelineReducer from './slice.ts';

export const store = configureStore({
  reducer: {
    pipeline: pipelineReducer,
    [api.reducerPath]: api.reducer,
  },
  middleware: (getDefaultMiddleware) =>
    getDefaultMiddleware().concat(api.middleware),
});

export type RootState = ReturnType<typeof store.getState>;
export type AppDispatch = typeof store.dispatch;
