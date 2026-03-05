from fastapi import APIRouter, WebSocket, WebSocketDisconnect
import logging
import asyncio
from typing import List

router = APIRouter(tags=["AudioStream"])
logger = logging.getLogger(__name__)

# ── Connected listener clients (browser) ─────────────────────
listener_clients: List[WebSocket] = []

# ── Track ESP32 audio source ─────────────────────────────────
esp_audio_connected: bool = False


@router.websocket("/ws/audio-stream")
async def audio_stream_from_esp(ws: WebSocket):
    """
    ESP32 connects here and continuously sends raw PCM audio frames.
    Each binary frame is relayed to every connected browser listener.
    Protocol: ESP32 sends binary frames (raw 16-bit PCM, 16 kHz, mono).
    """
    global esp_audio_connected
    await ws.accept()
    esp_audio_connected = True
    logger.info("[AudioStream] ESP32 audio source connected.")

    try:
        while True:
            data = await ws.receive_bytes()

            # Relay to all browser listeners
            dead: List[WebSocket] = []
            for client in listener_clients:
                try:
                    await client.send_bytes(data)
                except Exception:
                    dead.append(client)
            for d in dead:
                if d in listener_clients:
                    listener_clients.remove(d)

    except WebSocketDisconnect:
        logger.info("[AudioStream] ESP32 audio source disconnected.")
    except Exception as e:
        logger.error(f"[AudioStream] ESP32 error: {e}")
    finally:
        esp_audio_connected = False


@router.websocket("/ws/audio-listen")
async def audio_listen(ws: WebSocket):
    """
    Browser clients connect here to receive real-time audio.
    They receive binary PCM frames (16-bit, 16 kHz, mono) decoded via Web Audio API.
    """
    await ws.accept()
    listener_clients.append(ws)
    count = len(listener_clients)
    logger.info(f"[AudioStream] Browser listener connected. Total: {count}, ESP32 source: {esp_audio_connected}")

    try:
        # Send a text message so the browser knows the connection is live
        await ws.send_text("connected")

        while True:
            # Keep alive — client may send ping/control messages
            msg = await ws.receive_text()
            if msg == "ping":
                await ws.send_text("pong")
    except WebSocketDisconnect:
        pass
    except Exception as e:
        logger.error(f"[AudioStream] Listener error: {e}")
    finally:
        if ws in listener_clients:
            listener_clients.remove(ws)
        logger.info(f"[AudioStream] Browser listener disconnected. Total: {len(listener_clients)}")
