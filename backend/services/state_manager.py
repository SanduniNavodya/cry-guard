import asyncio
import json
import time
import logging
from typing import List, Dict, Any
from fastapi import WebSocket

logger = logging.getLogger(__name__)


class StateManager:
    """
    Central state manager that tracks ESP32 data and broadcasts to WebSocket clients.
    """

    def __init__(self):
        self.connected_clients: List[WebSocket] = []

        # ESP32 connection tracking
        self.esp_connected: bool = False
        self.esp_last_seen: float = 0
        self.ESP_TIMEOUT: float = 10.0  # seconds before marking as disconnected

        # Latest sensor data
        self.sensor_data: Dict[str, Any] = {
            "temperature": None,
            "humidity": None,
            "motion": False,
            "light_dark": False,
            "timestamp": None,
        }

        # Latest cry detection result
        self.cry_status: Dict[str, Any] = {
            "cry_detected": False,
            "message": "No data yet",
            "timestamp": None,
        }

        # Notification history (last 50)
        self.notifications: List[Dict[str, Any]] = []

    # ── WebSocket Client Management ──────────────────────────

    async def register(self, ws: WebSocket):
        await ws.accept()
        self.connected_clients.append(ws)
        logger.info(f"WebSocket client connected. Total: {len(self.connected_clients)}")
        # Send current state immediately
        await self._send_to(ws, self._full_state())

    def unregister(self, ws: WebSocket):
        if ws in self.connected_clients:
            self.connected_clients.remove(ws)
        logger.info(f"WebSocket client disconnected. Total: {len(self.connected_clients)}")

    # ── Broadcast Helpers ────────────────────────────────────

    async def _broadcast(self, message: dict):
        dead: List[WebSocket] = []
        for ws in self.connected_clients:
            try:
                await ws.send_text(json.dumps(message))
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.unregister(ws)

    async def _send_to(self, ws: WebSocket, message: dict):
        try:
            await ws.send_text(json.dumps(message))
        except Exception:
            self.unregister(ws)

    # ── State Updates ────────────────────────────────────────

    def update_esp_status(self, connected: bool = True):
        self.esp_connected = connected
        self.esp_last_seen = time.time()

    def is_esp_connected(self) -> bool:
        if self.esp_last_seen == 0:
            return False
        return (time.time() - self.esp_last_seen) < self.ESP_TIMEOUT

    async def update_sensor_data(self, data: dict):
        self.sensor_data = {
            "temperature": data.get("temperature"),
            "humidity": data.get("humidity"),
            "motion": data.get("motion", False),
            "light_dark": data.get("light_dark", False),
            "timestamp": time.time(),
        }
        self.update_esp_status(connected=True)

        await self._broadcast({
            "type": "sensor_update",
            "data": self.sensor_data,
            "esp_connected": True,
        })

    async def update_cry_status(self, result: dict):
        self.cry_status = result

        if result.get("cry_detected"):
            notification = {
                "type": "cry_alert",
                "message": result.get("message", "Baby is crying!"),
                "timestamp": time.time(),
            }
            self.notifications.append(notification)
            # Keep last 50
            self.notifications = self.notifications[-50:]

            await self._broadcast({
                "type": "cry_alert",
                "data": result,
                "notification": notification,
            })
        else:
            await self._broadcast({
                "type": "cry_update",
                "data": result,
            })

    # ── Full State Snapshot ──────────────────────────────────

    def _full_state(self) -> dict:
        return {
            "type": "full_state",
            "esp_connected": self.is_esp_connected(),
            "sensor_data": self.sensor_data,
            "cry_status": self.cry_status,
            "notifications": self.notifications[-10:],
        }


# Singleton
state_manager = StateManager()
