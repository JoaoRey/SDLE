#include "gtest/gtest.h"
#include "CRDTCounter.h"

TEST(CounterTestSuite, StartsAtZero){
    CRDTCounter c("X");
    EXPECT_EQ(c.get_value(), 0);
}

TEST(CounterTestSuite, IncrementAndDecrementAmounts){
    CRDTCounter c("X");
    c.increment(5);
    c.decrement(2);
    c.increment(3);
    EXPECT_EQ(c.get_value(), 6); // 5 - 2 + 3
}

TEST(CounterTestSuite, NegativeValueAfterMoreDecrements){
    CRDTCounter c("X");
    c.decrement(3);
    EXPECT_EQ(c.get_value(), -3);
    c.increment(1);
    EXPECT_EQ(c.get_value(), -2);
}

TEST(CounterTestSuite, MergeAggregatesPerNode){
    CRDTCounter a("A");
    CRDTCounter b("B");

    a.increment(2);
    b.increment(4);
    b.decrement(1);
    a.decrement(1);

    // Commutative
    a.merge(b);
    EXPECT_EQ(a.get_value(), 4);
    b.merge(a);
    EXPECT_EQ(b.get_value(), 4);

    // Idempotence
    const int before = a.get_value();
    a.merge(b);
    EXPECT_EQ(a.get_value(), before);
}