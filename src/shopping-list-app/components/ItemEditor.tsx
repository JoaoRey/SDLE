"use client";

import { useState } from 'react';
import styles from './ItemEditor.module.css';

interface ItemEditorProps {
  onAddItem: (name: string, quantity: number) => void;
}

export default function ItemEditor({ onAddItem }: ItemEditorProps) {
  const [name, setName] = useState('');
  const [quantity, setQuantity] = useState(1);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (name.trim()) {
      onAddItem(name.trim(), quantity);
      setName('');
      setQuantity(1);
    }
  };

  return (
    <form onSubmit={handleSubmit} className={styles.form}>
      <h3 className={styles.heading}>Add New Item</h3>
      <div className={styles.inputs}>
        <input
          type="text"
          value={name}
          onChange={(e) => setName(e.target.value)}
          placeholder="Item name"
          className={styles.inputName}
          required
        />
        <input
          type="number"
          value={quantity}
          onChange={(e) => setQuantity(Number(e.target.value))}
          min="1"
          className={styles.inputQuantity}
          required
        />
        <button type="submit" className={"btn btn-primary"}>Add</button>
      </div>
    </form>
  );
}