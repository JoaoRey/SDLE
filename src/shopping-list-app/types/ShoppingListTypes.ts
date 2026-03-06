export interface CRDTSet {
  adds: { [key: string]: string[] };
  removes: { [key: string]: string[] };
}

export interface CRDTCounter {
  increments: { [key: string]: number };
  decrements: { [key: string]: number };
}

export interface CRDTFlag {
  enables: string[];
  disables: string[];
}

export interface CRDTShoppingList {
  list_name: string;
  items: CRDTSet;
  quantities: { [itemName: string]: CRDTCounter };
  checks: { [itemName: string]: CRDTFlag };
}

// CRDT with sync metadata
export interface CRDTWithMetadata {
  crdt: CRDTShoppingList;
  lastModified: number;
  lastSyncTime: number | null;
  isDeleted?: boolean; // Soft delete flag
}

// Simple view of list for UI display
export interface ShoppingListItem {
  name: string;
  quantity: number;
  checked: boolean;
}

export interface ShoppingList {
  id: string;
  name: string;
  items: ShoppingListItem[];
}

export interface SyncStatus {
  isOnline: boolean;
  isSyncing: boolean;
  pendingChanges: number;
}
