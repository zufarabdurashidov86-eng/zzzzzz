"""
AI Voice Assistant - Server tomoni
==================================

ESP8266 dan WAV audio qabul qiladi -> Whisper bilan STT (uz/en) ->
LLM (OpenAI) ga savol -> qisqa javobni text qaytaradi (OLED uchun mos).

Ishga tushirish:
    pip install -r requirements.txt
    cp .env.example .env   # va OPENAI_API_KEY ni kiriting
    uvicorn main:app --host 0.0.0.0 --port 8000

Sinov:
    curl -X POST "http://localhost:8000/ask?lang=uz" \
         -H "Content-Type: audio/wav" --data-binary @test.wav
"""

import io
import os
import re
import logging
from typing import Optional

from fastapi import FastAPI, Request, Query, HTTPException
from fastapi.responses import PlainTextResponse
from dotenv import load_dotenv

import openai
from faster_whisper import WhisperModel

load_dotenv()

OPENAI_API_KEY = os.getenv("OPENAI_API_KEY", "")
WHISPER_MODEL = os.getenv("WHISPER_MODEL", "small")  # tiny/base/small/medium
WHISPER_DEVICE = os.getenv("WHISPER_DEVICE", "cpu")  # "cuda" agar GPU bo'lsa
LLM_MODEL = os.getenv("LLM_MODEL", "gpt-4o-mini")
MAX_ANSWER_CHARS = int(os.getenv("MAX_ANSWER_CHARS", "300"))

if not OPENAI_API_KEY:
    raise RuntimeError("OPENAI_API_KEY .env faylida ko'rsatilmagan")

openai.api_key = OPENAI_API_KEY

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("voice-ai")

log.info("Whisper modelini yuklash: %s (%s)", WHISPER_MODEL, WHISPER_DEVICE)
whisper = WhisperModel(WHISPER_MODEL, device=WHISPER_DEVICE, compute_type="int8")

app = FastAPI(title="ESP8266 AI Voice Assistant")


SYSTEM_PROMPT_UZ = (
    "Siz ixcham va aniq javob beruvchi yordamchisiz. "
    "Javobni 1-3 jumlada bering. Maksimum 280 belgi. "
    "Javob tilini foydalanuvchi tilida saqlang (o'zbek yoki ingliz). "
    "Markdown, kod bloklari yoki ortiqcha belgilarsiz, sof matn."
)


def transcribe(wav_bytes: bytes, lang_hint: Optional[str]) -> tuple[str, str]:
    """WAV baytlardan matn va aniqlangan tilni qaytaradi."""
    # faster-whisper file-like object qabul qiladi
    audio = io.BytesIO(wav_bytes)

    lang = None
    if lang_hint in ("uz", "en"):
        lang = lang_hint

    segments, info = whisper.transcribe(
        audio,
        language=lang,            # None bo'lsa avtomatik aniqlaydi
        vad_filter=True,
        beam_size=1,
    )
    text = " ".join(s.text.strip() for s in segments).strip()
    detected = info.language or (lang or "uz")
    return text, detected


def ask_llm(question: str, lang: str) -> str:
    """OpenAI ga savol jo'natadi va qisqa javob qaytaradi."""
    response = openai.chat.completions.create(
        model=LLM_MODEL,
        messages=[
            {"role": "system", "content": SYSTEM_PROMPT_UZ},
            {"role": "user", "content": question},
        ],
        temperature=0.4,
        max_tokens=200,
    )
    answer = response.choices[0].message.content.strip()
    # OLED uchun normalizatsiya
    answer = re.sub(r"\s+", " ", answer)
    if len(answer) > MAX_ANSWER_CHARS:
        answer = answer[: MAX_ANSWER_CHARS - 1].rstrip() + "…"
    return answer


@app.get("/health", response_class=PlainTextResponse)
def health() -> str:
    return "ok"


@app.post("/ask", response_class=PlainTextResponse)
async def ask(
    request: Request,
    lang: str = Query("auto", description="uz | en | auto"),
) -> str:
    body = await request.body()
    if not body or len(body) < 200:
        raise HTTPException(status_code=400, detail="Audio juda kichik yoki bo'sh")

    log.info("Audio qabul qilindi: %d bayt, lang=%s", len(body), lang)

    try:
        question, detected = transcribe(body, lang if lang in ("uz", "en") else None)
    except Exception as exc:
        log.exception("STT xatosi")
        raise HTTPException(status_code=500, detail=f"STT xato: {exc}") from exc

    if not question:
        return "Eshitmadim, qaytadan urining"

    log.info("Savol [%s]: %s", detected, question)

    try:
        answer = ask_llm(question, detected)
    except Exception as exc:
        log.exception("LLM xatosi")
        raise HTTPException(status_code=500, detail=f"LLM xato: {exc}") from exc

    log.info("Javob: %s", answer)
    return answer
