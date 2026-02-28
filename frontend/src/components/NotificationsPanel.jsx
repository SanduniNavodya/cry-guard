import React from 'react';
import { Bell } from 'lucide-react';

function formatTime(ts) {
  if (!ts) return '';
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

export default function NotificationsPanel({ notifications }) {
  // Show newest first
  const sorted = [...notifications].reverse();

  return (
    <div className="notifications-panel">
      <h2>
        <Bell size={18} /> Notifications
      </h2>

      {sorted.length === 0 ? (
        <div className="empty-state">No cry alerts yet. Monitoring...</div>
      ) : (
        <ul className="notification-list">
          {sorted.map((n, i) => (
            <li key={`${n.timestamp}-${i}`} className="notification-item">
              <div className="notification-dot" />
              <div className="notification-content">
                <p>{n.message}</p>
                <span>{formatTime(n.timestamp)}</span>
              </div>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
