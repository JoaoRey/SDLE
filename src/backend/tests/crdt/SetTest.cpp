#include "gtest/gtest.h"
#include "CRDTSet.h"

TEST(SetTestSuite, StartsEmpty){
    const CRDTSet s("A");
    EXPECT_FALSE(s.contains("x"));
    EXPECT_TRUE(s.get_values().empty());
}

TEST(SetTestSuite, SingleAddContains){
    CRDTSet s("A");
    s.add("x", "t1");
    EXPECT_TRUE(s.contains("x"));
    const unordered_set vals = s.get_values();
    EXPECT_EQ(vals.size(), 1);
    EXPECT_TRUE(vals.contains("x"));
}

TEST(SetTestSuite, RemoveMakesValueAbsent){
    CRDTSet s("A");
    s.add("x", "t1");
    s.remove("x");
    EXPECT_FALSE(s.contains("x"));
    EXPECT_TRUE(s.get_values().empty());
}

TEST(SetTestSuite, DuplicateAddsSingleRemoveKeepsValue){
    CRDTSet s("A");
    s.add("x", "t1");
    s.add("x", "t2");
    s.remove("x");
    EXPECT_FALSE(s.contains("x"));
    s.add("x", "t3");
    EXPECT_TRUE(s.contains("x"));
}

TEST(SetTestSuite, RemoveNonExistentIsNoOp){
    CRDTSet s("A");
    s.remove("x");
    EXPECT_FALSE(s.contains("x"));
}

TEST(SetTestSuite, MergeUnionOfAdds){
    CRDTSet a("A");
    CRDTSet b("B");
    a.add("x", "t1");
    b.add("y", "t2");

    a.merge(b);
    b.merge(a);

    unordered_set av = a.get_values();
    unordered_set bv = b.get_values();
    EXPECT_EQ(av.size(), 2);
    EXPECT_EQ(bv.size(), 2);
    EXPECT_TRUE(a.contains("x"));
    EXPECT_TRUE(a.contains("y"));
    EXPECT_TRUE(b.contains("x"));
    EXPECT_TRUE(b.contains("y"));
}

TEST(SetTestSuite, MergeRemovalsDoNotDeleteOtherReplicaTag){
    CRDTSet a("A");
    CRDTSet b("B");
    a.add("x", "t1");
    b.add("x", "t2");

    a.remove("x");
    a.merge(b);
    b.merge(a);

    // Value should remain because t2 remains
    EXPECT_TRUE(a.contains("x"));
    EXPECT_TRUE(b.contains("x"));
}

TEST(SetTestSuite, MergeCommutativityAndIdempotence){
    CRDTSet a("A");
    CRDTSet b("B");
    a.add("x", "t1");
    b.add("x", "t2");
    b.add("y", "t3");

    a.merge(b);
    b.merge(a);

    const unordered_set av1 = a.get_values();
    const unordered_set bv1 = b.get_values();
    EXPECT_EQ(av1, bv1);

    // Idempotent merges
    a.merge(b);
    b.merge(a);
    EXPECT_EQ(a.get_values(), av1);
    EXPECT_EQ(b.get_values(), bv1);
}
