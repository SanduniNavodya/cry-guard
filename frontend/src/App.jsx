import React, { useState, useCallback } from 'react';
import { useWebSocket } from './hooks/useWebSocket';
import Header from './components/Header';
import CryBanner from './components/CryBanner';
import SensorGrid from './components/SensorGrid';
import NotificationsPanel from './components/NotificationsPanel';

export default function App() {
  const [espConnected, setEspConnected] = useState(false);
  const [sensorData, setSensorData] = useState({
    temperature: null,
    humidity: null,
    motion: false,
    light_dark: false,
    timestamp: null,
  });
  const [cryStatus, setCryStatus] = useState({
    cry_detected: false,
    message: 'Waiting for data...',
    timestamp: null,
  });
  const [notifications, setNotifications] = useState([]);

  const handleWsMessage = useCallback((msg) => {
    switch (msg.type) {
      case 'full_state':
        setEspConnected(msg.esp_connected);
        if (msg.sensor_data) setSensorData(msg.sensor_data);
        if (msg.cry_status) setCryStatus(msg.cry_status);
        if (msg.notifications) setNotifications(msg.notifications);
        break;

      case 'sensor_update':
        setEspConnected(msg.esp_connected ?? true);
        if (msg.data) setSensorData(msg.data);
        break;

      case 'cry_alert':
        if (msg.data) setCryStatus(msg.data);
        if (msg.notification) {
          setNotifications((prev) => [...prev, msg.notification].slice(-50));
          // Browser notification
          if (Notification.permission === 'granted') {
            new Notification('CryGuard Alert', {
              body: msg.notification.message,
              icon: '/vite.svg',
            });
          }
        }
        break;

      case 'cry_update':
        if (msg.data) setCryStatus(msg.data);
        break;

      default:
        break;
    }
  }, []);

  useWebSocket(handleWsMessage);

  // Request notification permission on first render
  React.useEffect(() => {
    if ('Notification' in window && Notification.permission === 'default') {
      Notification.requestPermission();
    }
  }, []);

  return (
    <div className="app">
      <Header espConnected={espConnected} />
      <CryBanner cryStatus={cryStatus} />
      <SensorGrid sensorData={sensorData} />
      <NotificationsPanel notifications={notifications} />
    </div>
  );
}
