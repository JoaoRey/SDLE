#include "gtest/gtest.h"
#include "CRDTFlag.h"

TEST(FlagTestSuite, StartsEnabled){
    const CRDTFlag flag("A");
    EXPECT_TRUE(flag.is_enabled());
}

TEST(FlagTestSuite, EnableAddsTag){
    CRDTFlag flag("A");
    EXPECT_TRUE(flag.is_enabled());
    flag.enable("e1");
    EXPECT_TRUE(flag.is_enabled());
}

TEST(FlagTestSuite, DisableTurnsOff){
    CRDTFlag flag("A");
    EXPECT_TRUE(flag.is_enabled()); // Starts enabled
    flag.disable();
    EXPECT_FALSE(flag.is_enabled());
}

TEST(FlagTestSuite, EnableWinsOverConcurrentDisable){
    CRDTFlag a("A");
    CRDTFlag b("B");

    // Both start enabled, B disables, A enables with new tag
    b.disable();
    a.enable("t1");

    a.merge(b);
    b.merge(a);
    EXPECT_TRUE(a.is_enabled());
    EXPECT_TRUE(b.is_enabled());
}

TEST(FlagTestSuite, BothDisableStaysDisabled){
    CRDTFlag a("A");
    CRDTFlag b("B");

    a.disable();
    b.disable();

    a.merge(b);
    b.merge(a);

    EXPECT_FALSE(a.is_enabled());
    EXPECT_FALSE(b.is_enabled());
}

TEST(FlagTestSuite, MergeIdempotent){
    CRDTFlag a("A");
    CRDTFlag b("B");

    // Both start enabled, add more tags
    a.enable("x");
    b.enable("y");

    a.merge(b);
    b.merge(a);
    const bool before = a.is_enabled();
    a.merge(b);
    EXPECT_EQ(a.is_enabled(), before);
}

TEST(FlagTestSuite, DisablePropagatesCorrectly){
    CRDTFlag a("A");
    // Replica
    CRDTFlag b = a;

    a.enable("t1");

    a.disable();

    a.merge(b);
    b.merge(a);

    EXPECT_FALSE(a.is_enabled());
    EXPECT_FALSE(b.is_enabled());
}

TEST(FlagTestSuite, EnableOtherThanDisableWins){
    CRDTFlag a("A");
    CRDTFlag b = a;

    a.enable("t1");
    a.disable();

    b.enable("t2");

    a.merge(b);
    b.merge(a);

    EXPECT_TRUE(a.is_enabled());
    EXPECT_TRUE(b.is_enabled());
}

TEST(FlagTestSuite, MultipleEnablesWithDisable){
    CRDTFlag a("A");

    a.enable("t1");
    a.enable("t2");
    a.enable("t3");

    a.disable();

    EXPECT_FALSE(a.is_enabled());
}

TEST(FlagTestSuite, EnableAfterMergeWithDisable){
    CRDTFlag a("A");
    CRDTFlag b("B");

    // Both disable
    a.disable();
    b.disable();

    // Merge
    a.merge(b);
    b.merge(a);

    EXPECT_FALSE(a.is_enabled());
    EXPECT_FALSE(b.is_enabled());

    b.enable("t2");

    EXPECT_TRUE(b.is_enabled());
    a.merge(b);
    EXPECT_TRUE(a.is_enabled());
}

TEST(FlagTestSuite, DisableDoesNotAffectOtherEnables){
    CRDTFlag a("A");
    CRDTFlag b("B");

    a.disable();
    b.enable("t1");
    a.merge(b);
    b.merge(a);

    EXPECT_TRUE(a.is_enabled());
    EXPECT_TRUE(b.is_enabled());
}
