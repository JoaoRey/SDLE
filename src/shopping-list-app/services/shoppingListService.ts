/**
 * Shopping List Service - Local-First Architecture
 * 
 * This service implements a local-first approach where:
 * 1. All data is stored as CRDTs in localStorage
 * 2. When online, it syncs CRDT state to backend via API route
 * 3. Backend handles CRDT merge
 */

import { v4 as uuidv4 } from 'uuid';
import {
	CRDTSet,
	CRDTCounter,
	CRDTFlag,
	CRDTShoppingList,
	CRDTWithMetadata,
	ShoppingListItem,
	ShoppingList,
	SyncStatus
} from '../types/ShoppingListTypes';

export type {
	CRDTSet,
	CRDTCounter,
	CRDTFlag,
	CRDTShoppingList,
	CRDTWithMetadata,
	ShoppingListItem,
	ShoppingList,
	SyncStatus
};

const API_ROUTE = '/api/zmq';
const SYNC_INTERVAL = 5000; // Sync every 5 seconds when online
const CLIENT_ID = uuidv4();

class ShoppingListService {
	private syncStatus: SyncStatus = {
		isOnline: typeof window !== 'undefined' ? navigator.onLine : false,
		isSyncing: false,
		pendingChanges: 0,
	};

	private listeners: Set<(status: SyncStatus) => void> = new Set();
	private listChangeListeners: Map<string, Set<(list: ShoppingList) => void>> = new Map();
	private syncIntervals: Map<string, NodeJS.Timeout> = new Map();
	private syncingLists: Set<string> = new Set();

	constructor() {
		if (typeof window !== "undefined") {
			// Listen to browser online/offline events
			window.addEventListener('online', () => {
				this.syncStatus.isOnline = true;
				this.notifyListeners();
				// Sync all subscribed lists when coming online
				for (const listId of this.listChangeListeners.keys()) {
					this.syncList(listId);
				}
			});

			window.addEventListener('offline', () => {
				this.syncStatus.isOnline = false;
				this.notifyListeners();
			});
		}
	}

	/////////////////////////////////////////////////////////////
	// CRDT to localStorage
	/////////////////////////////////////////////////////////////

	private getListKey(listId: string): string {
		return `crdt_meta:${listId}`;
	}

	private loadCrdt(listId: string): CRDTWithMetadata {
		if (typeof window === "undefined") {
			return this.createEmptyCrdtMeta(listId);
		}

		const key = this.getListKey(listId);
		const stored = localStorage.getItem(key);

		if (stored) {
			try {
				return JSON.parse(stored) as CRDTWithMetadata;
			} catch (e) {
				console.error(`Failed to parse CRDT for ${listId}:`, e);
			}
		}

		return this.createEmptyCrdtMeta(listId);
	}

	private saveCrdt(
		listId: string,
		crdt: CRDTShoppingList,
		markModified: boolean = true,
		syncTime: number | null = null,
		isDeleted?: boolean
	): void {
		if (typeof window === "undefined") return;

		const key = this.getListKey(listId);
		const existing = this.loadCrdt(listId);

		const meta: CRDTWithMetadata = {
			crdt,
			lastModified: markModified ? Date.now() : existing.lastModified,
			lastSyncTime: syncTime !== null ? syncTime : existing.lastSyncTime,
			isDeleted: isDeleted !== undefined ? isDeleted : existing.isDeleted,
		};

		localStorage.setItem(key, JSON.stringify(meta));
	}

	private createEmptyCrdtMeta(listId: string): CRDTWithMetadata {
		return {
			crdt: {
				list_name: listId,
				items: { adds: {}, removes: {} },
				quantities: {},
				checks: {},
			},
			lastModified: Date.now(),
			lastSyncTime: null,
			isDeleted: false,
		};
	}

	private getAllListIds(): string[] {
		if (typeof window === "undefined") return [];

		const lists: string[] = [];
		for (let i = 0; i < localStorage.length; i++) {
			const key = localStorage.key(i);
			if (key?.startsWith("crdt_meta:")) {
				lists.push(key.substring(10));
			}
		}
		return lists;
	}

	/////////////////////////////////////////////////////////////
	// CRDT to UI Conversion
	/////////////////////////////////////////////////////////////

	private crdtToUIList(listId: string, meta: CRDTWithMetadata): ShoppingList {
		const crdt = meta.crdt;
		const items: ShoppingListItem[] = [];

		const addedItems = crdt.items.adds || {};
		const removedItems = crdt.items.removes || {};

		for (const itemName of Object.keys(addedItems)) {
			const addedTags = addedItems[itemName] || [];
			const removedTags = removedItems[itemName] || [];
			const activeTags = addedTags.filter((tag) => !removedTags.includes(tag));

			if (activeTags.length === 0) continue;

			// Calculate quantity from counter
			let quantity = 0;
			const counter = crdt.quantities[itemName];
			if (counter) {
				const increments = counter.increments || {};
				const decrements = counter.decrements || {};
				quantity =
					Object.values(increments).reduce((a, b) => a + (b as number), 0) -
					Object.values(decrements).reduce((a, b) => a + (b as number), 0);
			}

			// Calculate checked status from flag
			let checked = false;
			const flag = crdt.checks[itemName];
			if (flag) {
				const enabled = flag.enables || [];
				const disabled = flag.disables || [];
				checked = enabled.some((tag) => !disabled.includes(tag));
			}

			items.push({
				name: itemName,
				quantity: Math.max(0, quantity),
				checked,
			});
		}

		return {
			id: listId,
			name: listId,
			items,
		};
	}

	/////////////////////////////////////////////////////////////
	// API bridge
	/////////////////////////////////////////////////////////////
	private async sendToApi(type: string, listName: string, payload: any = {}): Promise<any> {
		const response = await fetch(API_ROUTE, {
			method: "POST",
			headers: { "Content-Type": "application/json" },
			body: JSON.stringify({
				type,
				list_name: listName,
				payload,
			}),
		});

		if (!response.ok) {
			const error = await response.json().catch(() => ({ error: response.statusText }));
			throw new Error(error.error || `HTTP ${response.status}`);
		}

		return response.json();
	}

	/////////////////////////////////////////////////////////////
	// Backend
	/////////////////////////////////////////////////////////////

	// Check if CRDT has any non-tombstoned items
	private hasActiveItems(crdt: CRDTShoppingList): boolean {
		const adds = crdt.items.adds || {};
		const removes = crdt.items.removes || {};

		for (const itemName of Object.keys(adds)) {
			const addTags = adds[itemName] || [];
			const removeTags = removes[itemName] || [];
			// If any add tag is not in removes, item is active
			if (addTags.some((tag) => !removeTags.includes(tag))) {
				return true;
			}
		}
		return false;
	}

	private isEmptyList(crdt: CRDTShoppingList): boolean {
		const hasItems = Object.keys(crdt.items.adds || {}).length > 0 ||
			Object.keys(crdt.items.removes || {}).length > 0;
		const hasQuantities = Object.keys(crdt.quantities || {}).length > 0;
		const hasChecks = Object.keys(crdt.checks || {}).length > 0;

		return !hasItems && !hasQuantities && !hasChecks;
	}

	private async syncList(listId: string): Promise<void> {
		if (typeof window === "undefined") return;

		// Avoid overlapping syncs for the same list
		if (this.syncingLists.has(listId)) return;
		this.syncingLists.add(listId);

		let meta = this.loadCrdt(listId);

		try {
			if (meta.isDeleted) {
				// Send PUT with fully tombstoned CRDT
				console.log("CRDT: Deleting list:", listId, meta.crdt);
				const putResponse = await this.sendToApi("PUT", listId, { data: meta.crdt });
				console.log("PUT response for delete:", putResponse);
				if (putResponse.payload?.status === 200 && putResponse.payload?.data) {
					const mergedCrdt = putResponse.payload.data as CRDTShoppingList;
					// Check if list was restored by another user adding items
					if (this.hasActiveItems(mergedCrdt)) {
						meta.isDeleted = false;
						meta.crdt = mergedCrdt;
						meta.lastSyncTime = Date.now();
						const key = this.getListKey(listId);
						localStorage.setItem(key, JSON.stringify(meta));
						const list = this.crdtToUIList(listId, meta);
						this.notifyListChangeListeners(listId, list);
					} else {
						// List remains deleted, remove from localStorage
						localStorage.removeItem(this.getListKey(listId));
						const interval = this.syncIntervals.get(listId);
						if (interval) {
							clearInterval(interval);
							this.syncIntervals.delete(listId);
						}
						this.listChangeListeners.delete(listId);
					}
					// Notify listeners to update pending count after delete sync
					this.notifyListeners();
				}

				// Mark as online on successful sync
				if (!this.syncStatus.isOnline) {
					this.syncStatus.isOnline = true;
					this.notifyListeners();
				}
			} else {
				const isFirstAccess = meta.lastSyncTime === null && this.isEmptyList(meta.crdt);
				const needsSync = meta.lastModified > (meta.lastSyncTime ?? 0);

				if (isFirstAccess) {
					console.log("First access to list, fetching from server:", listId);
					const getResponse = await this.sendToApi("GET", listId);
					console.log("GET response:", getResponse);
					if (getResponse.payload?.status === 200 && getResponse.payload?.data) {
						const remoteCrdt = getResponse.payload.data;
						this.saveCrdt(listId, remoteCrdt, false, Date.now());
						const updatedMeta = this.loadCrdt(listId);
						const list = this.crdtToUIList(listId, updatedMeta);
						this.notifyListChangeListeners(listId, list);
						// Notify listeners to update pending count after successful GET
						this.notifyListeners();
					}
				} else if (needsSync) {
					// We have local changes, PUT to backend (creates or merges)
					console.log("Local changes detected, syncing list:", listId, meta.crdt);
					const putResponse = await this.sendToApi("PUT", listId, { data: meta.crdt });
					console.log("PUT response:", putResponse);
					if (putResponse.payload?.status === 200 && putResponse.payload?.data) {
						// Save merged result from backend
						this.saveCrdt(listId, putResponse.payload.data, false, Date.now());
						const updatedMeta = this.loadCrdt(listId);
						const list = this.crdtToUIList(listId, updatedMeta);
						this.notifyListChangeListeners(listId, list);
						// Notify listeners to update pending count after successful PUT
						this.notifyListeners();
					}
				} else {
					// No local changes, GET remote state
					const getResponse = await this.sendToApi("GET", listId);
					console.log("GET response:", getResponse);
					if (getResponse.payload?.status === 200 && getResponse.payload?.data) {
						const remoteCrdt = getResponse.payload.data;
						this.saveCrdt(listId, remoteCrdt, false, Date.now());
						const list = this.crdtToUIList(listId, meta);
						this.notifyListChangeListeners(listId, list);
						// Notify listeners to update pending count after successful GET
						this.notifyListeners();
					}
				}

				// Mark as online on successful sync
				if (!this.syncStatus.isOnline) {
					this.syncStatus.isOnline = true;
					this.notifyListeners();
				}
			}
		} catch (error) {
			// Mark as offline on sync failure
			if (this.syncStatus.isOnline) {
				this.syncStatus.isOnline = false;
				this.notifyListeners();
			}
		} finally {
			this.syncingLists.delete(listId);
		}
	}

	/////////////////////////////////////////////////////////////
	// List Operations
	/////////////////////////////////////////////////////////////

	getKnownLists(): string[] {
		if (typeof window === "undefined") return [];

		const listsWithTime: { id: string; lastModified: number }[] = [];
		for (let i = 0; i < localStorage.length; i++) {
			const key = localStorage.key(i);
			if (key?.startsWith("crdt_meta:")) {
				const listId = key.substring(10);
				const meta = this.loadCrdt(listId);
				if (!meta.isDeleted) {
					listsWithTime.push({ id: listId, lastModified: meta.lastModified });
				}
			}
		}
		// Sort by most recent first
		listsWithTime.sort((a, b) => b.lastModified - a.lastModified);
		return listsWithTime.map(item => item.id);
	}

	async getList(listId: string): Promise<ShoppingList> {
		// Return local data immediately (local-first)
		const meta = this.loadCrdt(listId);
		const list = this.crdtToUIList(listId, meta);

		// Sync in background, don't wait
		this.syncList(listId);

		return list;
	}

	async deleteList(listId: string): Promise<boolean> {
		if (typeof window === "undefined") return false;

		const meta = this.loadCrdt(listId);
		const crdt = meta.crdt;

		// Tombstone all items
		const itemNames = Object.keys(crdt.items.adds || {});
		if (!crdt.items.removes) crdt.items.removes = {};
		for (const itemName of itemNames) {
			const addTags = crdt.items.adds[itemName] || [];
			crdt.items.removes[itemName] = [...addTags];

			if (crdt.quantities[itemName]) {
				const counter = crdt.quantities[itemName];
				if (!counter.decrements) counter.decrements = {};

				const increments = counter.increments || {};
				for (const [clientId, value] of Object.entries(increments)) {
					const currentDecrement = counter.decrements[clientId] || 0;
					const currentIncrement = value as number;
					if (currentIncrement > currentDecrement) {
						counter.decrements[clientId] = currentIncrement;
					}
				}
			}

			if (crdt.checks[itemName]) {
				const flag = crdt.checks[itemName];
				if (!flag.disables) flag.disables = [];

				const enables = flag.enables || [];
				for (const tag of enables) {
					if (!flag.disables.includes(tag)) {
						flag.disables.push(tag);
					}
				}
			}
		}

		// Mark as deleted locally
		meta.isDeleted = true;
		meta.lastModified = Date.now();
		const key = this.getListKey(listId);
		localStorage.setItem(key, JSON.stringify(meta));

		// Sync deletion in background
		this.syncList(listId);
		this.notifyListeners();

		return true;
	}

	/////////////////////////////////////////////////////////////
	// Item Operations
	/////////////////////////////////////////////////////////////

	async addItem(listId: string, name: string, quantity: number): Promise<ShoppingList> {
		const meta = this.loadCrdt(listId);
		const crdt = meta.crdt;

		// Add item to CRDT set with unique tag
		const tag = `${CLIENT_ID}:${Date.now()}`;
		if (!crdt.items.adds) crdt.items.adds = {};
		if (!crdt.items.adds[name]) crdt.items.adds[name] = [];
		crdt.items.adds[name].push(tag);

		// Initialize and set quantity
		if (!crdt.quantities[name]) {
			crdt.quantities[name] = { increments: {}, decrements: {} };
		}
		if (!crdt.quantities[name].increments) {
			crdt.quantities[name].increments = {};
		}
		crdt.quantities[name].increments[CLIENT_ID] =
			(crdt.quantities[name].increments[CLIENT_ID] || 0) + quantity;

		// Initialize check flag
		if (!crdt.checks[name]) {
			crdt.checks[name] = { enables: [], disables: [] };
		}

		// Clear deleted flag when adding items - for list restoration
		this.saveCrdt(listId, crdt, true, null, false);
		const list = this.crdtToUIList(listId, this.loadCrdt(listId));

		// Sync in background
		this.syncList(listId);
		this.notifyListeners();

		return list;
	}

	async removeItem(listId: string, itemName: string): Promise<ShoppingList> {
		const meta = this.loadCrdt(listId);
		const crdt = meta.crdt;

		// Mark all item tags as removed (tombstone)
		if (crdt.items.adds && crdt.items.adds[itemName]) {
			if (!crdt.items.removes) crdt.items.removes = {};
			crdt.items.removes[itemName] = [...(crdt.items.adds[itemName] || [])];
		}

		// Tombstone quantity: zero out by adding decrements equal to increments
		if (crdt.quantities[itemName]) {
			const counter = crdt.quantities[itemName];
			if (!counter.decrements) counter.decrements = {};

			// Calculate total to zero out
			const increments = counter.increments || {};
			for (const [clientId, value] of Object.entries(increments)) {
				const currentDecrement = counter.decrements[clientId] || 0;
				const currentIncrement = value as number;
				if (currentIncrement > currentDecrement) {
					counter.decrements[clientId] = currentIncrement;
				}
			}
		}

		// Tombstone check flag: disable by copying all enables to disables
		if (crdt.checks[itemName]) {
			const flag = crdt.checks[itemName];
			if (!flag.disables) flag.disables = [];

			const enables = flag.enables || [];
			for (const tag of enables) {
				if (!flag.disables.includes(tag)) {
					flag.disables.push(tag);
				}
			}
		}

		this.saveCrdt(listId, crdt);
		const list = this.crdtToUIList(listId, this.loadCrdt(listId));

		// Sync in background
		this.syncList(listId);
		this.notifyListeners();

		return list;
	}

	async toggleItemChecked(listId: string, itemName: string): Promise<ShoppingList> {
		const meta = this.loadCrdt(listId);
		const crdt = meta.crdt;

		// Initialize flag if needed
		if (!crdt.checks[itemName]) {
			crdt.checks[itemName] = { enables: [], disables: [] };
		}

		const flag = crdt.checks[itemName];
		const tag = `${CLIENT_ID}:${Date.now()}`;
		const isCurrentlyChecked =
			flag.enables && flag.enables.some((t) => !(flag.disables || []).includes(t));

		if (isCurrentlyChecked) {
			// Uncheck: disable all enabled tags
			if (!flag.disables) flag.disables = [];
			if (flag.enables) {
				flag.enables.forEach((t) => {
					if (!flag.disables!.includes(t)) {
						flag.disables!.push(t);
					}
				});
			}
		} else {
			// Check: enable with new tag
			if (!flag.enables) flag.enables = [];
			flag.enables.push(tag);
		}

		this.saveCrdt(listId, crdt);
		const list = this.crdtToUIList(listId, this.loadCrdt(listId));

		// Sync in background
		this.syncList(listId);
		this.notifyListeners();

		return list;
	}

	async updateItemQuantity(listId: string, itemName: string, quantity: number): Promise<ShoppingList> {
		const meta = this.loadCrdt(listId);
		const crdt = meta.crdt;

		// Initialize counter if needed
		if (!crdt.quantities[itemName]) {
			crdt.quantities[itemName] = { increments: {}, decrements: {} };
		}

		const counter = crdt.quantities[itemName];
		if (!counter.increments) counter.increments = {};
		if (!counter.decrements) counter.decrements = {};

		// Calculate current value and apply diff
		const currentValue =
			Object.values(counter.increments).reduce((a: number, b: any) => a + b, 0) -
			Object.values(counter.decrements).reduce((a: number, b: any) => a + b, 0);

		const diff = quantity - currentValue;

		if (diff > 0) {
			counter.increments[CLIENT_ID] = (counter.increments[CLIENT_ID] || 0) + diff;
		} else if (diff < 0) {
			counter.decrements[CLIENT_ID] = (counter.decrements[CLIENT_ID] || 0) + Math.abs(diff);
		}

		this.saveCrdt(listId, crdt);
		const list = this.crdtToUIList(listId, this.loadCrdt(listId));

		// Sync in background
		this.syncList(listId);
		this.notifyListeners();

		return list;
	}

	/////////////////////////////////////////////////////////////
	// Client Subscriptions
	/////////////////////////////////////////////////////////////

	private notifyListeners(): void {
		// Recalculate pending changes from metadata
		this.syncStatus.pendingChanges = 0;
		for (const listId of this.getAllListIds()) {
			const meta = this.loadCrdt(listId);
			if (meta.isDeleted || meta.lastModified > (meta.lastSyncTime ?? 0)) {
				this.syncStatus.pendingChanges++;
			}
		}

		this.listeners.forEach((cb) => cb({ ...this.syncStatus }));
	}

	private notifyListChangeListeners(listId: string, list: ShoppingList): void {
		const listeners = this.listChangeListeners.get(listId);
		if (listeners) {
			listeners.forEach((cb) => cb(list));
		}
	}

	onSyncStatusChange(callback: (status: SyncStatus) => void): () => void {
		this.listeners.add(callback);
		return () => this.listeners.delete(callback);
	}

	onListChange(listId: string, callback: (list: ShoppingList) => void): () => void {
		if (!this.listChangeListeners.has(listId)) {
			this.listChangeListeners.set(listId, new Set());
		}
		this.listChangeListeners.get(listId)!.add(callback);

		// Start periodic sync for this list
		if (!this.syncIntervals.has(listId)) {
			const interval = setInterval(() => this.syncList(listId), SYNC_INTERVAL);
			this.syncIntervals.set(listId, interval);
		}

		return () => {
			const listeners = this.listChangeListeners.get(listId);
			if (listeners) {
				listeners.delete(callback);
				if (listeners.size === 0) {
					// Stop syncing when no more listeners
					const interval = this.syncIntervals.get(listId);
					if (interval) {
						clearInterval(interval);
						this.syncIntervals.delete(listId);
					}
					this.listChangeListeners.delete(listId);
				}
			}
		};
	}

	getSyncStatus(): SyncStatus {
		return { ...this.syncStatus };
	}

	async sync(): Promise<void> {
		// Sync all lists with pending changes
		for (const listId of this.getAllListIds()) {
			const meta = this.loadCrdt(listId);
			if (meta.isDeleted || meta.lastModified > (meta.lastSyncTime ?? 0)) {
				await this.syncList(listId);
			}
		}

		this.notifyListeners();
	}
}

// Export singleton instance
export const shoppingListService = new ShoppingListService();
export default shoppingListService;
