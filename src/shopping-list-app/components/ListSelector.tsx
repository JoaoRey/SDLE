"use client";

import { useState, useEffect } from 'react';
import { shoppingListService } from '../services/shoppingListService';
import styles from './ListSelector.module.css';
import { v4 as uuidv4 } from 'uuid';

interface ListSelectorProps {
  lists: string[];
  onSelectList: (listId: string) => void;
}

const ITEMS_PER_PAGE = 5;

export default function ListSelector({ onSelectList }: ListSelectorProps) {
  const [newListId, setNewListName] = useState('');
  const [knownLists, setKnownLists] = useState<string[]>([]);
  const [currentPage, setCurrentPage] = useState(0);

  // Load known lists from localStorage on mount
  useEffect(() => {
    const lists = shoppingListService.getKnownLists();
    setKnownLists(lists);
  }, []);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (newListId.trim()) {
      onSelectList(newListId.trim());
      setNewListName('');
    }
  };

  const handleCreateNew = () => {
    // Generate a unique ID for new lists
    const newId = uuidv4();
    onSelectList(newId);
  };

  // Pagination logic
  const totalPages = Math.ceil(knownLists.length / ITEMS_PER_PAGE);
  const startIndex = currentPage * ITEMS_PER_PAGE;
  const visibleLists = knownLists.slice(startIndex, startIndex + ITEMS_PER_PAGE);

  const handlePrevPage = () => {
    setCurrentPage(prev => Math.max(0, prev - 1));
  };

  const handleNextPage = () => {
    setCurrentPage(prev => Math.min(totalPages - 1, prev + 1));
  };

  return (
    <div className={styles.container}>
      <h2>Enter your Shopping List ID</h2>
      <form onSubmit={handleSubmit} className={styles.form}>
        <input
          type="text"
          value={newListId}
          onChange={(e) => setNewListName(e.target.value)}
          placeholder="Enter a list ID or URL"
          className={styles.input}
        />
        <button type="submit" className={styles.button}>Enter</button>
      </form>

      <div className={styles.divider}>
        <span>or</span>
      </div>

      <button onClick={handleCreateNew} className={styles.createButton}>
        Create New List
      </button>

      {knownLists.length > 0 && (
        <div className={styles.existingLists}>
          <h3>Recent Lists:</h3>
          <ul>
            {visibleLists.map(listId => (
              <li key={listId} onClick={() => onSelectList(listId)}>
                <span className={styles.listId}>{listId}</span>
              </li>
            ))}
          </ul>
          {totalPages > 1 && (
            <div className={styles.pagination}>
              <button
                onClick={handlePrevPage}
                disabled={currentPage === 0}
                className={styles.pageButton}
                aria-label="Previous page"
              >
                ←
              </button>
              <span className={styles.pageInfo}>
                {currentPage + 1} / {totalPages}
              </span>
              <button
                onClick={handleNextPage}
                disabled={currentPage === totalPages - 1}
                className={styles.pageButton}
                aria-label="Next page"
              >
                →
              </button>
            </div>
          )}
        </div>
      )}
    </div>
  );
}