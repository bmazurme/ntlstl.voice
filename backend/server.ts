import 'dotenv/config';
import express from 'express';
import cors from 'cors';
import { PORT } from './config.js';
import router from './router.js';

const app = express();
app.use(cors());
app.use(express.json());
app.use(router);

app.listen(PORT, () => {
  console.log(`STT/LLM backend listening on http://localhost:${PORT}`);
});
