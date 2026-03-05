import React, { useState, useCallback } from 'react';
import { useWebSocket } from './hooks/useWebSocket';
import Header from './components/Header';
import CryBanner from './components/CryBanner';
import SensorGrid from './components/SensorGrid';
import ListenButton from './components/ListenButton';
import NotificationsPanel from './components/NotificationsPanel';
import CryAlertPopup from './components/CryAlertPopup';

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
  const [activeAlert, setActiveAlert] = useState(null);

  const handleCryAlert = useCallback((data, notification) => {
    setCryStatus(data);

    const notif = notification || {
      type: 'cry_alert',
      message: data.message || 'Baby is crying!',
      timestamp: data.timestamp || Date.now() / 1000,
      source: data.source || 'unknown',
    };

    setNotifications((prev) => [...prev, notif].slice(-50));

    // Show popup
    setActiveAlert(notif);

    // Browser notification (in addition to in-app popup)
    if (Notification.permission === 'granted') {
      new Notification('🚨 CryGuard Alert', {
        body: notif.message,
        icon: '/vite.svg',
        tag: 'cry-alert',   // prevent duplicate browser notifications
        requireInteraction: true,
      });
    }
  }, []);

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
        if (msg.data) {
          handleCryAlert(msg.data, msg.notification);
        }
        break;

      case 'cry_update':
        if (msg.data) setCryStatus(msg.data);
        break;

      default:
        break;
    }
  }, [handleCryAlert]);

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
      <ListenButton onCryAlert={handleCryAlert} />
      <NotificationsPanel notifications={notifications} />
      <CryAlertPopup
        alert={activeAlert}
        onDismiss={() => setActiveAlert(null)}
      />
    </div>
  );
}
