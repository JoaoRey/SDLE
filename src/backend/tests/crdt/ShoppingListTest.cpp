#include "gtest/gtest.h"
#include "CRDTShoppingList.h"
#include <unordered_set>

using namespace std;

TEST(ShoppingListSuite, StartsEmpty) {
    const CRDTShoppingList list("A", "Groceries");
    EXPECT_FALSE(list.contains("milk"));
}

TEST(ShoppingListSuite, AddAndRemoveMembership) {
    CRDTShoppingList list("A", "Groceries");
    list.add_item("milk", "t1");
    EXPECT_TRUE(list.contains("milk"));
    auto items = list.get_item_names();
    EXPECT_EQ(items.size(), 1);
    EXPECT_TRUE(items.contains("milk"));

    list.remove_item("milk");
    EXPECT_FALSE(list.contains("milk"));
}

TEST(ShoppingListSuite, QuantityDoesNothingWhenAbsent) {
    CRDTShoppingList list("A", "Groceries");
    list.increment("bread", 2);
    EXPECT_EQ(list.get_quantity("bread"), 0);
}

TEST(ShoppingListSuite, QuantityWhenPresent) {
    CRDTShoppingList list("A", "Groceries");
    list.add_item("bread", "t1");
    list.increment("bread", 2);
    list.decrement("bread", 1);
    list.increment("bread", 3);
    EXPECT_EQ(list.get_quantity("bread"), 4);
}

TEST(ShoppingListSuite, CheckAndUncheckFlag) {
    CRDTShoppingList list("A", "Groceries");
    list.add_item("eggs", "t1");

    EXPECT_TRUE(list.is_checked("eggs"));

    list.uncheck("eggs");
    EXPECT_FALSE(list.is_checked("eggs"));

    list.check("eggs", "tag2");
    EXPECT_TRUE(list.is_checked("eggs"));
}

TEST(ShoppingListSuite, MergeCommutativeIdempotent) {
    CRDTShoppingList a("A", "Groceries");
    CRDTShoppingList b("B", "Groceries");

    a.add_item("milk", "a1");
    b.add_item("bread", "b1");

    a.increment("milk", 2);
    b.increment("bread", 3);
    b.check("bread", "b2");

    a.merge(b);
    b.merge(a);

    // Items should be union
    const auto ai = a.get_item_names();
    const auto bi = b.get_item_names();
    EXPECT_EQ(ai, bi);
    EXPECT_TRUE(a.contains("milk"));
    EXPECT_TRUE(a.contains("bread"));

    // Quantities
    EXPECT_EQ(a.get_quantity("milk"), 2);
    EXPECT_EQ(a.get_quantity("bread"), 3);

    // Flags
    EXPECT_EQ(a.is_checked("bread"), b.is_checked("bread"));

    // Idempotence
    const auto before_items = a.get_item_names();
    const auto qm = a.get_quantity("milk");
    const auto qb = a.get_quantity("bread");
    a.merge(b);
    EXPECT_EQ(a.get_item_names(), before_items);
    EXPECT_EQ(a.get_quantity("milk"), qm);
    EXPECT_EQ(a.get_quantity("bread"), qb);
}

TEST(ShoppingListSuite, ConcurrentAddRemove) {
    CRDTShoppingList a("A", "Groceries");
    CRDTShoppingList b("B", "Groceries");

    a.add_item("apples", "t1");
    b.add_item("apples", "t2");

    // One replica removes after its add
    a.remove_item("apples");

    a.merge(b);
    b.merge(a);

    EXPECT_TRUE(a.contains("apples"));
    EXPECT_TRUE(b.contains("apples"));
}

TEST(ShoppingListSuite, NameMismatchNoMerge) {
    CRDTShoppingList a("A", "Groceries");
    CRDTShoppingList b("B", "Hardware");

    a.add_item("milk", "t1");
    b.add_item("milk", "t2");

    a.merge(b);

    // Should not see b's item; still only a's own additions
    EXPECT_TRUE(a.contains("milk"));
}

TEST(ShoppingListSuite, RemoveItemTombstonesQuantityAndCheck) {
    CRDTShoppingList list("A", "Groceries");
    
    // Add item with quantity and check it
    list.add_item("milk", "t1");
    list.increment("milk", 5);
    list.check("milk", "check1");
    
    EXPECT_TRUE(list.contains("milk"));
    EXPECT_EQ(list.get_quantity("milk"), 5);
    EXPECT_TRUE(list.is_checked("milk"));
    
    list.remove_item("milk");
    
    EXPECT_FALSE(list.contains("milk"));
    EXPECT_EQ(list.get_quantity("milk"), 0);
    EXPECT_FALSE(list.is_checked("milk"));
}

TEST(ShoppingListSuite, RemoveItemTombstoningPreserves) {
    CRDTShoppingList a("A", "Groceries");
    CRDTShoppingList b("B", "Groceries");
    
    a.add_item("milk", "a1");
    b.add_item("milk", "b1");
    
    a.increment("milk", 3);
    b.increment("milk", 2);
    
    a.remove_item("milk");
    
    b.add_item("milk", "b2");
    b.increment("milk", 4);
    b.check("milk", "check1");
    
    a.merge(b);
    b.merge(a);
    
    EXPECT_TRUE(a.contains("milk"));
    EXPECT_TRUE(b.contains("milk"));
    EXPECT_EQ(a.get_quantity("milk"), 6);
    EXPECT_EQ(b.get_quantity("milk"), 6);
    EXPECT_TRUE(a.is_checked("milk"));
    EXPECT_TRUE(b.is_checked("milk"));
}
