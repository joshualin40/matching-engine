#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <limits>
#include <list>
#include <random>
#include <stdexcept>
#include <vector>

#include "types.h"

using namespace me;

namespace {

constexpr Price kPrice = 100;
constexpr Quantity kMaxQty = std::numeric_limits<Quantity>::max();

class PriceLevelTest : public ::testing::Test {
protected:
    PriceLevel level;
    std::deque<Order> storage; // deque: stable addresses as orders are added

    Order* make(OrderId id, Quantity qty, Price price = kPrice) {
        return &storage.emplace_back(id, price, qty, Side::Buy, OrderType::Limit);
    }

    Order* add(OrderId id, Quantity qty, Price price = kPrice) {
        Order* o = make(id, qty, price);
        level.append(o);
        return o;
    }

    // Ids walking head -> tail via next pointers. Independent of validate().
    std::vector<OrderId> forwardIds() const {
        std::vector<OrderId> ids;
        for (Order* o = level.head; o && ids.size() <= storage.size(); o = o->next) ids.push_back(o->id);
        return ids;
    }

    // Ids walking tail -> head via prev pointers, reversed back to head-first order.
    std::vector<OrderId> backwardIds() const {
        std::vector<OrderId> ids;
        for (Order* o = level.tail; o && ids.size() <= storage.size(); o = o->prev) ids.push_back(o->id);
        std::reverse(ids.begin(), ids.end());
        return ids;
    }

    // Checks the list is linked both ways in the expected order with the expected total.
    void expectLevel(const std::vector<OrderId>& ids, Quantity total) {
        EXPECT_EQ(forwardIds(), ids);
        EXPECT_EQ(backwardIds(), ids);
        EXPECT_EQ(level.totalquantity, total);
        EXPECT_EQ(level.isEmpty(), ids.empty());
        if (ids.empty()) {
            EXPECT_EQ(level.head, nullptr);
            EXPECT_EQ(level.tail, nullptr);
        } else {
            ASSERT_NE(level.head, nullptr);
            ASSERT_NE(level.tail, nullptr);
            EXPECT_EQ(level.head->id, ids.front());
            EXPECT_EQ(level.tail->id, ids.back());
            EXPECT_EQ(level.head->prev, nullptr);
            EXPECT_EQ(level.tail->next, nullptr);
        }
        EXPECT_TRUE(level.validate());
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Empty level
// ---------------------------------------------------------------------------

TEST_F(PriceLevelTest, DefaultConstructedIsEmpty) {
    EXPECT_TRUE(level.isEmpty());
    EXPECT_EQ(level.head, nullptr);
    EXPECT_EQ(level.tail, nullptr);
    EXPECT_EQ(level.totalquantity, 0u);
}

TEST_F(PriceLevelTest, EmptyLevelValidates) {
    EXPECT_TRUE(level.validate());
}

TEST_F(PriceLevelTest, EmptyLevelWithNonZeroTotalIsInvalid) {
    level.totalquantity = 5;
    EXPECT_FALSE(level.validate());
}

// ---------------------------------------------------------------------------
// append
// ---------------------------------------------------------------------------

TEST_F(PriceLevelTest, AppendSingleOrder) {
    Order* o = add(1, 10);
    EXPECT_EQ(level.head, o);
    EXPECT_EQ(level.tail, o);
    EXPECT_EQ(o->next, nullptr);
    EXPECT_EQ(o->prev, nullptr);
    expectLevel({1}, 10);
}

TEST_F(PriceLevelTest, AppendTwoOrdersLinksBothWays) {
    Order* a = add(1, 10);
    Order* b = add(2, 20);
    EXPECT_EQ(a->next, b);
    EXPECT_EQ(b->prev, a);
    expectLevel({1, 2}, 30);
}

TEST_F(PriceLevelTest, AppendManyPreservesFifoOrder) {
    for (OrderId id = 1; id <= 10; ++id) add(id, 1);
    expectLevel({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 10);
}

TEST_F(PriceLevelTest, AppendAccumulatesTotalQuantity) {
    add(1, 5);
    EXPECT_EQ(level.totalquantity, 5u);
    add(2, 7);
    EXPECT_EQ(level.totalquantity, 12u);
    add(3, 100);
    EXPECT_EQ(level.totalquantity, 112u);
}

TEST_F(PriceLevelTest, AppendLargeSingleOrderAccepted) {
    // More than half of Quantity's range, but still fits.
    add(1, kMaxQty / 2 + 1);
    expectLevel({1}, kMaxQty / 2 + 1);
}

TEST_F(PriceLevelTest, AppendUpToExactlyMaxAccepted) {
    add(1, kMaxQty - 1);
    EXPECT_NO_THROW(add(2, 1));
    expectLevel({1, 2}, kMaxQty);
}

TEST_F(PriceLevelTest, AppendOverflowThrows) {
    add(1, kMaxQty);
    Order* extra = make(2, 1);
    EXPECT_THROW(level.append(extra), std::runtime_error);
}

TEST_F(PriceLevelTest, AppendOverflowLeavesLevelUnchanged) {
    add(1, kMaxQty - 5);
    Order* extra = make(2, 10);
    EXPECT_THROW(level.append(extra), std::runtime_error);
    EXPECT_EQ(extra->prev, nullptr);
    EXPECT_EQ(extra->next, nullptr);
    expectLevel({1}, kMaxQty - 5);
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

TEST_F(PriceLevelTest, RemoveNullIsNoOp) {
    add(1, 10);
    add(2, 20);
    level.remove(nullptr);
    expectLevel({1, 2}, 30);
}

TEST_F(PriceLevelTest, RemoveOnlyOrderEmptiesLevel) {
    Order* o = add(1, 10);
    level.remove(o);
    expectLevel({}, 0);
}

TEST_F(PriceLevelTest, RemoveHeadOfTwo) {
    Order* a = add(1, 10);
    add(2, 20);
    level.remove(a);
    expectLevel({2}, 20);
}

TEST_F(PriceLevelTest, RemoveTailOfTwo) {
    add(1, 10);
    Order* b = add(2, 20);
    level.remove(b);
    expectLevel({1}, 10);
}

TEST_F(PriceLevelTest, RemoveHeadOfMany) {
    Order* a = add(1, 10);
    add(2, 20);
    add(3, 30);
    level.remove(a);
    expectLevel({2, 3}, 50);
}

TEST_F(PriceLevelTest, RemoveTailOfMany) {
    add(1, 10);
    add(2, 20);
    Order* c = add(3, 30);
    level.remove(c);
    expectLevel({1, 2}, 30);
}

TEST_F(PriceLevelTest, RemoveMiddleRelinksNeighbours) {
    Order* a = add(1, 10);
    Order* b = add(2, 20);
    Order* c = add(3, 30);
    level.remove(b);
    EXPECT_EQ(a->next, c);
    EXPECT_EQ(c->prev, a);
    expectLevel({1, 3}, 40);
}

TEST_F(PriceLevelTest, RemoveAdjacentMiddleOrders) {
    add(1, 1);
    Order* b = add(2, 2);
    Order* c = add(3, 3);
    add(4, 4);
    level.remove(b);
    expectLevel({1, 3, 4}, 8);
    level.remove(c);
    expectLevel({1, 4}, 5);
}

TEST_F(PriceLevelTest, DrainFromHeadInFifoOrder) {
    // Mirrors how the matcher consumes a level: always remove the head.
    for (OrderId id = 1; id <= 5; ++id) add(id, static_cast<Quantity>(id));
    std::vector<OrderId> drained;
    while (!level.isEmpty()) {
        drained.push_back(level.head->id);
        level.remove(level.head);
        EXPECT_TRUE(level.validate());
    }
    EXPECT_EQ(drained, (std::vector<OrderId>{1, 2, 3, 4, 5}));
    expectLevel({}, 0);
}

TEST_F(PriceLevelTest, DrainFromTail) {
    for (OrderId id = 1; id <= 5; ++id) add(id, 1);
    while (!level.isEmpty()) {
        level.remove(level.tail);
        EXPECT_TRUE(level.validate());
    }
    expectLevel({}, 0);
}

TEST_F(PriceLevelTest, RemoveAfterPartialFillUsesCurrentQuantity) {
    // The matcher decrements order->quantity and totalquantity together before removing.
    Order* a = add(1, 10);
    add(2, 20);
    a->quantity -= 4;
    level.totalquantity -= 4;
    EXPECT_TRUE(level.validate());
    level.remove(a);
    expectLevel({2}, 20);
}

TEST_F(PriceLevelTest, ReuseAfterEmptying) {
    Order* a = add(1, 10);
    level.remove(a);
    expectLevel({}, 0);
    add(2, 5);
    add(3, 6);
    expectLevel({2, 3}, 11);
}

TEST_F(PriceLevelTest, AppendAfterRemovingTailLinksToNewTail) {
    add(1, 1);
    Order* b = add(2, 2);
    level.remove(b);
    add(3, 3);
    expectLevel({1, 3}, 4);
}

TEST_F(PriceLevelTest, AppendAfterRemovingHeadKeepsOrder) {
    Order* a = add(1, 1);
    add(2, 2);
    level.remove(a);
    add(3, 3);
    expectLevel({2, 3}, 5);
}

// ---------------------------------------------------------------------------
// validate: detects corrupted state
// ---------------------------------------------------------------------------

TEST_F(PriceLevelTest, ValidateDetectsHeadWithoutTail) {
    add(1, 10);
    level.tail = nullptr;
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsTailWithoutHead) {
    add(1, 10);
    level.head = nullptr;
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsHeadWithPrev) {
    Order* a = add(1, 10);
    Order* stray = make(99, 0);
    a->prev = stray;
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsTailWithNext) {
    Order* b = add(1, 10);
    Order* stray = make(99, 0);
    b->next = stray;
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsCycle) {
    add(1, 1);
    Order* b = add(2, 1);
    Order* c = add(3, 1);
    add(4, 1);
    c->next = b; // 1 -> 2 -> 3 -> 2 -> ...
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsSelfLoop) {
    add(1, 1);
    Order* b = add(2, 1);
    add(3, 1);
    b->next = b;
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsBrokenBackLink) {
    Order* a = add(1, 1);
    add(2, 1);
    Order* c = add(3, 1);
    c->prev = a; // should be b
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsTotalTooHigh) {
    add(1, 10);
    add(2, 20);
    level.totalquantity += 1;
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsTotalTooLow) {
    add(1, 10);
    add(2, 20);
    level.totalquantity -= 1;
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsMixedPrices) {
    add(1, 10, 100);
    add(2, 10, 100);
    add(3, 10, 101);
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateDetectsMixedPriceAtHead) {
    Order* a = add(1, 10, 100);
    add(2, 10, 100);
    a->price = 99;
    EXPECT_FALSE(level.validate());
}

TEST_F(PriceLevelTest, ValidateAcceptsLongWellFormedList) {
    for (OrderId id = 1; id <= 100; ++id) add(id, static_cast<Quantity>(id));
    EXPECT_TRUE(level.validate());
    EXPECT_EQ(level.totalquantity, 5050u);
}

// ---------------------------------------------------------------------------
// Randomised: compare against std::list as a reference model
// ---------------------------------------------------------------------------

TEST_F(PriceLevelTest, RandomOpsMatchReferenceModel) {
    std::mt19937 rng(12345);
    std::list<Order*> model;
    Quantity modelTotal = 0;
    OrderId nextId = 1;

    for (int step = 0; step < 5000; ++step) {
        const bool doAppend = model.empty() || std::uniform_int_distribution<int>(0, 2)(rng) != 0;
        if (doAppend) {
            const auto qty = std::uniform_int_distribution<Quantity>(1, 1000)(rng);
            Order* o = add(nextId++, qty);
            model.push_back(o);
            modelTotal += qty;
        } else {
            auto it = model.begin();
            std::advance(it, std::uniform_int_distribution<std::size_t>(0, model.size() - 1)(rng));
            level.remove(*it);
            modelTotal -= (*it)->quantity;
            model.erase(it);
        }

        std::vector<OrderId> expected;
        for (Order* o : model) expected.push_back(o->id);
        ASSERT_EQ(forwardIds(), expected) << "step " << step;
        ASSERT_EQ(backwardIds(), expected) << "step " << step;
        ASSERT_EQ(level.totalquantity, modelTotal) << "step " << step;
        ASSERT_EQ(level.isEmpty(), model.empty()) << "step " << step;
        ASSERT_TRUE(level.validate()) << "step " << step;
    }
}
