"use client";

import { Item } from '../app/page';
import styles from './ItemList.module.css';

interface ItemListProps {
  items: Item[];
  onToggleItemStatus: (itemId: string) => void;
  onDeleteItem: (itemId: string) => void;
}

export default function ItemList({ items, onToggleItemStatus, onDeleteItem }: ItemListProps) {
  if (items.length === 0) {
    return <p className={styles.emptyMessage}>This list is empty. Add an item to get started!</p>;
  }

  return (
    <div className={styles.container}>
      <ul className={styles.list}>
        {items.map(item => (
          <li
            key={item.id}
            className={`${styles.item} ${item.status === 'Bought' ? styles.bought : ''}`}
          >
            <div className={styles.itemContent} onClick={() => onToggleItemStatus(item.id)}>
              <span className={styles.itemName}>
                {item.name}
              </span>
              <span className={styles.itemQuantity}>×{item.quantity}</span>
            </div>
            <div className={styles.actions}>
              <button
                onClick={() => onToggleItemStatus(item.id)}
                className={`btn btn-primary${item.status === 'Bought' ? ' ' + styles.doneButton : ''}`}
                aria-label={item.status === 'To buy' ? 'Mark as bought' : 'Mark as needed'}
              >
                {item.status === 'To buy' ? 'Needed' : 'Bought'}
              </button>
              <button
                onClick={() => onDeleteItem(item.id)}
                className={"btn btn-danger"}
                aria-label="Delete item"
              >
                <svg
                  xmlns="http://www.w3.org/2000/svg"
                  width="20" height="20"
                  viewBox="0 0 24 24"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="1.5"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                  style={{ display: 'inline-block', verticalAlign: 'middle' }}
                >
                  <rect x="5" y="7" width="14" height="12" rx="2" />
                  <path d="M3 7h18M9 7V5a2 2 0 0 1 4 0v2" />
                </svg>
              </button>
            </div>
          </li>
        ))}
      </ul>
    </div>
  );
}