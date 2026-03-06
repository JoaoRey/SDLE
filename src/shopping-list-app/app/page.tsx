"use client";

import { useState, useEffect, useCallback } from 'react';
import ListSelector from '../components/ListSelector';
import ListView from '../components/ListView';
import SyncIndicator from '../components/SyncIndicator';
import ToastContainer, { showToast } from '../components/Toast';
import { shoppingListService, ShoppingList, SyncStatus } from '../services/shoppingListService';
import styles from './page.module.css';

export interface Item {
  id: string;
  name: string;
  quantity: number;
  status: 'To buy' | 'Bought';
}

export default function ShoppingListApp() {
  const [currentListId, setCurrentListId] = useState<string | null>(null);
  const [currentList, setCurrentList] = useState<ShoppingList | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [syncStatus, setSyncStatus] = useState<SyncStatus>({
    isOnline: false,
    isSyncing: false,
    pendingChanges: 0,
  });

  // Page transition state
  const [isTransitioning, setIsTransitioning] = useState(false);
  const [displayedView, setDisplayedView] = useState<'selector' | 'list'>('selector');

  // Subscribe to sync status changes
  useEffect(() => {
    const unsubscribe = shoppingListService.onSyncStatusChange(setSyncStatus);
    setSyncStatus(shoppingListService.getSyncStatus());
    return () => unsubscribe();
  }, []);

  // Load list and subscribe to changes when currentListId changes
  useEffect(() => {
    if (!currentListId) {
      setCurrentList(null);
      setError(null);
      return;
    }

    let isMounted = true;

    const loadList = async () => {
      setIsLoading(true);
      setError(null);
      try {
        const list = await shoppingListService.getList(currentListId);
        if (isMounted) {
          setCurrentList(list);
        }
      } catch (err) {
        console.error('Failed to load list:', err);
        if (isMounted && syncStatus.isOnline) {
          setError(err instanceof Error ? err.message : 'Failed to load list');
        }
      } finally {
        if (isMounted) {
          setIsLoading(false);
        }
      }
    };

    loadList();

    // Subscribe to real-time updates for this list
    const unsubscribe = shoppingListService.onListChange(currentListId, (updatedList: ShoppingList) => {
      if (isMounted) {
        setCurrentList(updatedList);
      }
    });

    return () => {
      isMounted = false;
      unsubscribe();
    };
  }, [currentListId, syncStatus.isOnline]);

  // Handle page transitions
  const transitionToView = useCallback((view: 'selector' | 'list', listId: string | null) => {
    setIsTransitioning(true);

    // Wait for exit animation
    setTimeout(() => {
      setCurrentListId(listId);
      setDisplayedView(view);

      // Wait a tick then start enter animation
      setTimeout(() => {
        setIsTransitioning(false);
      }, 50);
    }, 300);
  }, []);

  const handleSelectList = useCallback((listId: string) => {
    transitionToView('list', listId);
  }, [transitionToView]);

  const handleUnselectList = useCallback(() => {
    transitionToView('selector', null);
  }, [transitionToView]);

  const handleAddItem = useCallback(async (name: string, quantity: number) => {
    if (!currentListId) return;

    try {
      setError(null);
      const updatedList = await shoppingListService.addItem(currentListId, name, quantity);
      setCurrentList(updatedList);
      showToast(`Added "${name}" to list`, 'success');
    } catch (err) {
      console.error('Failed to add item:', err);
      if (syncStatus.isOnline) {
        setError(err instanceof Error ? err.message : 'Failed to add item');
        showToast('Failed to add item', 'error');
      }
    }
  }, [currentListId, syncStatus.isOnline]);

  const handleToggleItem = useCallback(async (itemName: string) => {
    if (!currentListId) return;

    try {
      setError(null);
      const updatedList = await shoppingListService.toggleItemChecked(currentListId, itemName);
      setCurrentList(updatedList);
    } catch (err) {
      console.error('Failed to toggle item:', err);
      if (syncStatus.isOnline) {
        setError(err instanceof Error ? err.message : 'Failed to toggle item');
      }
    }
  }, [currentListId, syncStatus.isOnline]);

  const handleDeleteItem = useCallback(async (itemName: string) => {
    if (!currentListId) return;

    try {
      setError(null);
      const updatedList = await shoppingListService.removeItem(currentListId, itemName);
      setCurrentList(updatedList);
      showToast(`Removed "${itemName}"`, 'info');
    } catch (err) {
      console.error('Failed to delete item:', err);
      if (syncStatus.isOnline) {
        setError(err instanceof Error ? err.message : 'Failed to delete item');
        showToast('Failed to delete item', 'error');
      }
    }
  }, [currentListId, syncStatus.isOnline]);

  const handleUpdateQuantity = useCallback(async (itemName: string, quantity: number) => {
    if (!currentListId) return;

    try {
      setError(null);
      const updatedList = await shoppingListService.updateItemQuantity(currentListId, itemName, quantity);
      setCurrentList(updatedList);
    } catch (err) {
      console.error('Failed to update quantity:', err);
      if (syncStatus.isOnline) {
        setError(err instanceof Error ? err.message : 'Failed to update quantity');
      }
    }
  }, [currentListId, syncStatus.isOnline]);

  const handleDeleteList = useCallback(async () => {
    if (!currentListId) return;

    try {
      setError(null);
      await shoppingListService.deleteList(currentListId);
      transitionToView('selector', null);
    } catch (err) {
      console.error('Failed to delete list:', err);
      if (syncStatus.isOnline) {
        setError(err instanceof Error ? err.message : 'Failed to delete list');
      } else {
        transitionToView('selector', null);
      }
    }
  }, [currentListId, syncStatus.isOnline, transitionToView]);

  const handleSync = useCallback(async () => {
    try {
      setError(null);
      await shoppingListService.sync();
      if (currentListId) {
        const list = await shoppingListService.getList(currentListId);
        setCurrentList(list);
      }
    } catch (err) {
      console.error('Failed to sync:', err);
      // Only show sync errors if we're supposed to be online
      if (syncStatus.isOnline) {
        setError(err instanceof Error ? err.message : 'Failed to sync');
      }
    }
  }, [currentListId, syncStatus.isOnline]);

  // Convert ShoppingList items to Item format for ListView
  const items: Item[] = currentList?.items.map((item: { name: string; quantity: number; checked: boolean }, index: number) => ({
    id: `${item.name}-${index}`,
    name: item.name,
    quantity: item.quantity,
    status: item.checked ? 'Bought' : 'To buy',
  })) || [];

  const pageClass = `${styles.pageContent} ${isTransitioning ? styles.transitioning : styles.visible}`;

  return (
    <main className={styles.main}>
      <SyncIndicator
        status={syncStatus}
        onSync={handleSync}
      />

      {error && (
        <div className={styles.errorBanner}>
          <span>ERROR {error}</span>
          <button onClick={() => setError(null)}>×</button>
        </div>
      )}

      <div className={pageClass}>
        {displayedView === 'selector' ? (
          <ListSelector
            onSelectList={handleSelectList}
            lists={[]}
          />
        ) : (
          <ListView
            listId={currentListId!}
            items={items}
            isLoading={isLoading}
            onAddItem={handleAddItem}
            onToggleItem={handleToggleItem}
            onDeleteItem={handleDeleteItem}
            onUpdateQuantity={handleUpdateQuantity}
            onUnselectList={handleUnselectList}
            onDeleteList={handleDeleteList}
          />
        )}
      </div>
      <ToastContainer />
    </main>
  );
}