"use client";

import { useState } from 'react';
import styles from './TopBar.module.css';

interface TopBarProps {
  listId: string;
  onUnselectList: () => void;
  onDeleteList: () => void;
}

export default function TopBar({ listId, onUnselectList, onDeleteList }: TopBarProps) {
  const [showDeleteConfirm, setShowDeleteConfirm] = useState(false);

  const handleCopyLink = () => {
    const url = `${window.location.origin}?list=${encodeURIComponent(listId)}`;
    navigator.clipboard.writeText(url).then(() => {
      // Could add a toast notification here
      alert('Link copied to clipboard!');
    }).catch(() => {
      alert('Failed to copy link');
    });
  };

  const handleDeleteClick = () => {
    setShowDeleteConfirm(true);
  };

  const handleConfirmDelete = () => {
    onDeleteList();
    setShowDeleteConfirm(false);
  };

  return (
    <>
      <div className={styles.container}>
        <button onClick={onUnselectList} className={styles.backButton} aria-label="Back to lists">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
            <path d="M19 12H5M12 19l-7-7 7-7"/>
          </svg>
        </button>
        
        <h2 className={styles.listName}>{listId}</h2>
        
        <div className={styles.actions}>
          <button onClick={handleCopyLink} className={styles.actionButton} title="Share list">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M4 12v8a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-8"/>
              <polyline points="16,6 12,2 8,6"/>
              <line x1="12" y1="2" x2="12" y2="15"/>
            </svg>
          </button>
          
          <button onClick={handleDeleteClick} className={`${styles.actionButton} ${styles.deleteButton}`} title="Delete list">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <polyline points="3,6 5,6 21,6"/>
              <path d="M19,6v14a2,2 0 0,1 -2,2H7a2,2 0 0,1 -2,-2V6m3,0V4a2,2 0 0,1 2,-2h4a2,2 0 0,1 2,2v2"/>
              <line x1="10" y1="11" x2="10" y2="17"/>
              <line x1="14" y1="11" x2="14" y2="17"/>
            </svg>
          </button>
        </div>
      </div>
      
      {showDeleteConfirm && (
        <div className={styles.modalOverlay} onClick={() => setShowDeleteConfirm(false)}>
          <div className={styles.modal} onClick={e => e.stopPropagation()}>
            <h3>Delete List?</h3>
            <p>Are you sure you want to delete &quot;{listId}&quot;? This action cannot be undone.</p>
            <div className={styles.modalActions}>
              <button onClick={() => setShowDeleteConfirm(false)} className="btn btn-secondary">
                Cancel
              </button>
              <button onClick={handleConfirmDelete} className="btn btn-danger">
                Delete
              </button>
            </div>
          </div>
        </div>
      )}
    </>
  );
}