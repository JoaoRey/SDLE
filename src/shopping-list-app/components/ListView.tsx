"use client";

import { Item } from '../app/page';
import TopBar from './TopBar';
import ItemList from './ItemList';
import ItemEditor from './ItemEditor';
import styles from './ListView.module.css';

interface ListViewProps {
  listId: string;
  items: Item[];
  isLoading?: boolean;
  onAddItem: (name: string, quantity: number) => void;
  onToggleItem: (itemName: string) => void;
  onDeleteItem: (itemName: string) => void;
  onUpdateQuantity: (itemName: string, quantity: number) => void;
  onUnselectList: () => void;
  onDeleteList: () => void;
}

export default function ListView({
  listId,
  items,
  isLoading,
  onAddItem,
  onToggleItem,
  onDeleteItem,
  onUpdateQuantity,
  onUnselectList,
  onDeleteList,
}: ListViewProps) {

  const handleToggleItemStatus = (itemId: string) => {
    const item = items.find(i => i.id === itemId);
    if (item) {
      onToggleItem(item.name);
    }
  };

  const handleDeleteItem = (itemId: string) => {
    const item = items.find(i => i.id === itemId);
    if (item) {
      onDeleteItem(item.name);
    }
  };

  return (
    <div className={styles.container}>
      <TopBar
        listId={listId}
        onUnselectList={onUnselectList}
        onDeleteList={onDeleteList}
      />
      <div className={styles.content}>
        <ItemEditor onAddItem={onAddItem} />
        {isLoading ? (
          <div className={styles.loadingContainer}>
            <div className={styles.loadingSpinner}></div>
            <p>Loading list...</p>
            <div className={styles.skeleton}>
              <div className={styles.skeletonItem}></div>
              <div className={styles.skeletonItem}></div>
              <div className={styles.skeletonItem}></div>
            </div>
          </div>
        ) : (
          <ItemList
            items={items}
            onToggleItemStatus={handleToggleItemStatus}
            onDeleteItem={handleDeleteItem}
          />
        )}
      </div>
    </div>
  );
}