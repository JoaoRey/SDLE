"use client";

import { SyncStatus } from '../services/shoppingListService';
import styles from './SyncIndicator.module.css';

interface SyncIndicatorProps {
  status: SyncStatus;
  onSync: () => void;
}

export default function SyncIndicator({ status, onSync }: SyncIndicatorProps) {
  const formatTime = (timestamp: number | null) => {
    if (!timestamp) return 'Never';
    const date = new Date(timestamp);
    return date.toLocaleTimeString();
  };

  return (
    <div className={styles.container}>
      <div className={styles.statusContainer}>
        <div className={`${styles.statusDot} ${status.isOnline ? styles.online : styles.offline}`} />
        <span className={styles.statusText}>
          {status.isOnline ? 'Online' : 'Offline'}
        </span>
      </div>
      
      {status.pendingChanges > 0 && (
        <span className={styles.pendingBadge}>
          {status.pendingChanges} pending
        </span>
      )}
      
      {status.isSyncing && (
        <span className={styles.syncingBadge}>
          Syncing...
        </span>
      )}
      
      <button 
        onClick={onSync} 
        className={`${styles.syncButton} ${status.isSyncing ? styles.syncing : ''}`}
        disabled={status.isSyncing}
        title={status.isSyncing ? 'Syncing...' : (status.isOnline ? 'Sync now' : 'Will sync when online')}
      >
        <svg 
          className={styles.syncIcon} 
          viewBox="0 0 24 24" 
          fill="none" 
          stroke="currentColor" 
          strokeWidth="2"
        >
          <path d="M21.5 2v6h-6M2.5 22v-6h6M2 11.5a10 10 0 0 1 18.8-4.3M22 12.5a10 10 0 0 1-18.8 4.3"/>
        </svg>
      </button>
    </div>
  );
}
