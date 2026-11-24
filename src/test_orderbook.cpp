#include <gtest/gtest.h>
#include "orderbook.h"

using namespace me;

namespace {

// Checks one trade's fields (buy id, sell id, price, quantity)
void expectTrade(const Trade& t, OrderId buy, OrderId sell, Price price, Quantity qty) {
    EXPECT_EQ(t.buyerid, buy);
    EXPECT_EQ(t.sellerid, sell);
    EXPECT_EQ(t.price, price);
    EXPECT_EQ(t.quantity, qty);
}

Order limitOrder(OrderId id, Side side, Price price, Quantity qty) {
    return Order(id, price, qty, side, OrderType::Limit);
}

// Builds a market order (price is ignored)
Order marketOrder(OrderId id, Side side, Quantity qty) {
    return Order(id, 0, qty, side, OrderType::Market);
}

// Adds an order that must be accepted and must not trade
void rest(Orderbook& book, OrderId id, Side side, Price price, Quantity qty) {
    auto trades = book.addOrder(limitOrder(id, side, price, qty));
    ASSERT_TRUE(trades.has_value());
    ASSERT_TRUE(trades->empty());
}

} // namespace

// ---------------------------------------------------------------------------
// Empty book
// ---------------------------------------------------------------------------

TEST(OrderbookTest, EmptyBook) {
    Orderbook book;
    EXPECT_FALSE(book.getBid());
    EXPECT_FALSE(book.getAsk());
    EXPECT_FALSE(book.cancelOrder(1));
    EXPECT_TRUE(book.validate());
}

// ---------------------------------------------------------------------------
// Resting and best prices
// ---------------------------------------------------------------------------

// Non-crossing orders rest on their own side
TEST(OrderbookTest, NonCrossingOrdersRest) {
    Orderbook book;
    rest(book, 1, Side::Buy, 99, 5);
    rest(book, 2, Side::Sell, 101, 5);
    EXPECT_EQ(book.getBid(), 99);
    EXPECT_EQ(book.getAsk(), 101);
    EXPECT_TRUE(book.validate());
}

// Best bid is the highest bid, best ask the lowest ask, regardless of arrival order
TEST(OrderbookTest, BestPrices) {
    Orderbook book;
    rest(book, 1, Side::Buy, 97, 1);
    rest(book, 2, Side::Buy, 99, 1);
    rest(book, 3, Side::Buy, 98, 1);
    rest(book, 4, Side::Sell, 103, 1);
    rest(book, 5, Side::Sell, 101, 1);
    rest(book, 6, Side::Sell, 102, 1);
    EXPECT_EQ(book.getBid(), 99);
    EXPECT_EQ(book.getAsk(), 101);
    EXPECT_TRUE(book.validate());
}

// Prices are signed; ordering must still hold below zero
TEST(OrderbookTest, NegativePrices) {
    Orderbook book;
    rest(book, 1, Side::Buy, -10, 1);
    rest(book, 2, Side::Buy, -5, 1);
    rest(book, 3, Side::Sell, -1, 1);
    rest(book, 4, Side::Sell, -3, 1);
    EXPECT_EQ(book.getBid(), -5);
    EXPECT_EQ(book.getAsk(), -3);
    auto trades = book.addOrder(limitOrder(5, Side::Buy, -3, 1));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), 5, 4, -3, 1);
    EXPECT_EQ(book.getAsk(), -1);
}

// ---------------------------------------------------------------------------
// Rejections
// ---------------------------------------------------------------------------

TEST(OrderbookTest, RejectZeroQuantity) {
    Orderbook book;
    auto res = book.addOrder(limitOrder(1, Side::Buy, 100, 0));
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), RejectReason::ZeroQuantity);
    EXPECT_FALSE(book.getBid());
    EXPECT_FALSE(book.cancelOrder(1));
}

TEST(OrderbookTest, RejectZeroQuantityMarket) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 5);
    auto res = book.addOrder(marketOrder(2, Side::Buy, 0));
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), RejectReason::ZeroQuantity);
    EXPECT_EQ(book.getAsk(), 100);
}

// Same id as a resting order on the same side
TEST(OrderbookTest, RejectDuplicateIdSameSide) {
    Orderbook book;
    rest(book, 1, Side::Buy, 100, 5);
    auto res = book.addOrder(limitOrder(1, Side::Buy, 99, 5));
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), RejectReason::DuplicateId);
    EXPECT_EQ(book.getBid(), 100);
    EXPECT_TRUE(book.validate());
}

// Duplicate id is rejected before matching, so no trade happens
TEST(OrderbookTest, RejectDuplicateIdWouldCross) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 5);
    auto res = book.addOrder(limitOrder(1, Side::Buy, 100, 5));
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), RejectReason::DuplicateId);
    EXPECT_EQ(book.getAsk(), 100);   // resting order untouched
    EXPECT_TRUE(book.cancelOrder(1));
}

// Ids are free again once the order leaves the book
TEST(OrderbookTest, IdReusableAfterCancel) {
    Orderbook book;
    rest(book, 1, Side::Buy, 100, 5);
    EXPECT_TRUE(book.cancelOrder(1));
    rest(book, 1, Side::Sell, 105, 5);
    EXPECT_EQ(book.getAsk(), 105);
    EXPECT_FALSE(book.getBid());
}

TEST(OrderbookTest, IdReusableAfterFill) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 5);
    ASSERT_TRUE(book.addOrder(limitOrder(2, Side::Buy, 100, 5)).has_value());
    rest(book, 1, Side::Buy, 90, 5);
    EXPECT_EQ(book.getBid(), 90);
}

// Pool full: new orders rejected until a slot is freed
TEST(OrderbookTest, RejectPoolFull) {
    Orderbook book;
    for (OrderId id = 1; id <= ORDER_CAPACITY; ++id) rest(book, id, Side::Buy, 100, 1);
    auto res = book.addOrder(limitOrder(ORDER_CAPACITY + 1, Side::Buy, 99, 1));
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), RejectReason::PoolFull);

    EXPECT_TRUE(book.cancelOrder(1));
    rest(book, ORDER_CAPACITY + 1, Side::Buy, 99, 1);
    EXPECT_EQ(book.getBid(), 100);
}

// Slots are recycled: the pool can be filled, drained and filled again
TEST(OrderbookTest, PoolReuseAfterFills) {
    Orderbook book;
    constexpr Quantity n = ORDER_CAPACITY - 1;   // leave room for the incoming order
    for (int round = 0; round < 3; ++round) {
        OrderId base = static_cast<OrderId>(round) * 10'000;
        for (OrderId i = 1; i <= n; ++i) rest(book, base + i, Side::Sell, 100, 1);
        auto trades = book.addOrder(marketOrder(base + 9'999, Side::Buy, n));
        ASSERT_TRUE(trades.has_value());
        EXPECT_EQ(trades->size(), n);
        EXPECT_FALSE(book.getAsk());
    }
}

// A full pool should not block orders that don't need a slot to rest
TEST(OrderbookTest, PoolFullStillAcceptsMarketOrder) {
    Orderbook book;
    for (OrderId id = 1; id <= ORDER_CAPACITY; ++id) rest(book, id, Side::Sell, 100, 1);
    auto trades = book.addOrder(marketOrder(ORDER_CAPACITY + 1, Side::Buy, 1));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), ORDER_CAPACITY + 1, 1, 100, 1);
}

// ---------------------------------------------------------------------------
// Limit matching
// ---------------------------------------------------------------------------

// Crossing at exactly the resting price trades
TEST(OrderbookTest, ExactPriceCross) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 5);
    auto trades = book.addOrder(limitOrder(2, Side::Buy, 100, 5));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), 2, 1, 100, 5);
    EXPECT_FALSE(book.getAsk());
    EXPECT_FALSE(book.getBid());
    EXPECT_TRUE(book.validate());
}

// One tick away from the ask does not trade
TEST(OrderbookTest, OneTickShortDoesNotTrade) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 5);
    rest(book, 2, Side::Buy, 99, 5);
    rest(book, 3, Side::Buy, 98, 5);
    auto trades = book.addOrder(limitOrder(4, Side::Sell, 101, 5));
    ASSERT_TRUE(trades.has_value());
    EXPECT_TRUE(trades->empty());
    EXPECT_EQ(book.getBid(), 99);
    EXPECT_EQ(book.getAsk(), 100);
}

// Aggressive buy far through the book trades at the resting price
TEST(OrderbookTest, PriceImprovementGoesToTaker) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 5);
    auto trades = book.addOrder(limitOrder(2, Side::Buy, 1'000, 5));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), 2, 1, 100, 5);
}

// Sweeps several levels, leftover rests at its own price
TEST(OrderbookTest, SweepLevels) {
    Orderbook book;
    book.addOrder(limitOrder(1, Side::Sell, 100, 2));
    book.addOrder(limitOrder(2, Side::Sell, 101, 2));
    book.addOrder(limitOrder(3, Side::Sell, 102, 2));
    auto trades = book.addOrder(limitOrder(4, Side::Buy, 101, 7));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 2u);
    expectTrade(trades->at(0), 4, 1, 100, 2);
    expectTrade(trades->at(1), 4, 2, 101, 2);
    EXPECT_EQ(book.getBid(), 101);   // leftover 3 rests
    EXPECT_EQ(book.getAsk(), 102);   // 102 untouched
    EXPECT_TRUE(book.validate());
}

// Leftover rests and can itself be filled later with the right quantity
TEST(OrderbookTest, LeftoverRestsWithRemainingQuantity) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 2);
    ASSERT_TRUE(book.addOrder(limitOrder(2, Side::Buy, 100, 5)).has_value());
    EXPECT_EQ(book.getBid(), 100);
    EXPECT_FALSE(book.getAsk());
    auto trades = book.addOrder(limitOrder(3, Side::Sell, 100, 10));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), 2, 3, 100, 3);   // only 3 left of order 2
    EXPECT_EQ(book.getAsk(), 100);              // 7 of order 3 rests
    EXPECT_FALSE(book.getBid());
}

// Same price fills in arrival order
TEST(OrderbookTest, Fifo) {
    Orderbook book;
    book.addOrder(limitOrder(1, Side::Sell, 100, 3));
    book.addOrder(limitOrder(2, Side::Sell, 100, 3));
    book.addOrder(limitOrder(3, Side::Sell, 100, 3));
    auto trades = book.addOrder(limitOrder(4, Side::Buy, 100, 5));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 2u);
    expectTrade(trades->at(0), 4, 1, 100, 3);
    expectTrade(trades->at(1), 4, 2, 100, 2);
    EXPECT_TRUE(book.validate());
}

// A partially filled resting order keeps its place at the front of the queue
TEST(OrderbookTest, PartialFillKeepsPriority) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 10);
    rest(book, 2, Side::Sell, 100, 10);
    ASSERT_TRUE(book.addOrder(limitOrder(3, Side::Buy, 100, 4)).has_value());
    auto trades = book.addOrder(limitOrder(4, Side::Buy, 100, 8));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 2u);
    expectTrade(trades->at(0), 4, 1, 100, 6);
    expectTrade(trades->at(1), 4, 2, 100, 2);
    EXPECT_TRUE(book.validate());
}

// Sell side mirror: incoming sell hits bids, trades at the bid's price
TEST(OrderbookTest, SellHitsBids) {
    Orderbook book;
    book.addOrder(limitOrder(1, Side::Buy, 102, 4));
    book.addOrder(limitOrder(2, Side::Buy, 101, 4));
    auto trades = book.addOrder(limitOrder(3, Side::Sell, 100, 6));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 2u);
    expectTrade(trades->at(0), 1, 3, 102, 4);   // resting price, not 100
    expectTrade(trades->at(1), 2, 3, 101, 2);
    EXPECT_EQ(book.getBid(), 101);
    EXPECT_FALSE(book.getAsk());
    EXPECT_TRUE(book.validate());
}

// Sell leftover rests on the ask side once bids are exhausted
TEST(OrderbookTest, SellLeftoverRests) {
    Orderbook book;
    rest(book, 1, Side::Buy, 100, 3);
    auto trades = book.addOrder(limitOrder(2, Side::Sell, 99, 10));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), 1, 2, 100, 3);
    EXPECT_FALSE(book.getBid());
    EXPECT_EQ(book.getAsk(), 99);
}

// Incoming exactly empties a level: level disappears, next level becomes best
TEST(OrderbookTest, ExactLevelFillRemovesLevel) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 3);
    rest(book, 2, Side::Sell, 100, 2);
    rest(book, 3, Side::Sell, 101, 1);
    auto trades = book.addOrder(limitOrder(4, Side::Buy, 100, 5));
    ASSERT_TRUE(trades.has_value());
    EXPECT_EQ(trades->size(), 2u);
    EXPECT_EQ(book.getAsk(), 101);
    EXPECT_FALSE(book.getBid());   // nothing left to rest
    EXPECT_TRUE(book.validate());
}

// Trade quantities always sum to what was taken
TEST(OrderbookTest, TradeQuantitiesSum) {
    Orderbook book;
    Quantity resting = 0;
    for (OrderId id = 1; id <= 20; ++id) {
        Quantity q = static_cast<Quantity>(id);
        rest(book, id, Side::Sell, 100 + static_cast<Price>(id % 4), q);
        resting += q;
    }
    auto trades = book.addOrder(limitOrder(100, Side::Buy, 103, 1'000));
    ASSERT_TRUE(trades.has_value());
    Quantity traded = 0;
    for (const auto& t : *trades) {
        EXPECT_EQ(t.buyerid, 100u);
        EXPECT_GT(t.quantity, 0u);
        traded += t.quantity;
    }
    EXPECT_EQ(traded, resting);
    EXPECT_FALSE(book.getAsk());
    EXPECT_EQ(book.getBid(), 103);
}

// Trades come out best price first even across many levels
TEST(OrderbookTest, TradesInPriceOrder) {
    Orderbook book;
    rest(book, 1, Side::Buy, 95, 1);
    rest(book, 2, Side::Buy, 99, 1);
    rest(book, 3, Side::Buy, 97, 1);
    auto trades = book.addOrder(limitOrder(4, Side::Sell, 90, 3));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 3u);
    EXPECT_EQ(trades->at(0).price, 99);
    EXPECT_EQ(trades->at(1).price, 97);
    EXPECT_EQ(trades->at(2).price, 95);
}

// ---------------------------------------------------------------------------
// Market orders
// ---------------------------------------------------------------------------

// Market order larger than the book: leftover is dropped
TEST(OrderbookTest, MarketRunsOut) {
    Orderbook book;
    book.addOrder(limitOrder(1, Side::Sell, 100, 3));
    auto trades = book.addOrder(marketOrder(2, Side::Buy, 10));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), 2, 1, 100, 3);
    EXPECT_FALSE(book.getAsk());
    EXPECT_FALSE(book.getBid());          // leftover 7 not rested
    EXPECT_FALSE(book.cancelOrder(2));    // and not in the lookup
    EXPECT_TRUE(book.validate());
}

// Market order into an empty book: accepted, no trades, nothing rests
TEST(OrderbookTest, MarketIntoEmptyBook) {
    Orderbook book;
    auto trades = book.addOrder(marketOrder(1, Side::Buy, 10));
    ASSERT_TRUE(trades.has_value());
    EXPECT_TRUE(trades->empty());
    EXPECT_FALSE(book.getBid());
    EXPECT_FALSE(book.cancelOrder(1));
}

// Market order ignores its own price and sweeps any level
TEST(OrderbookTest, MarketSellSweepsBids) {
    Orderbook book;
    rest(book, 1, Side::Buy, 100, 2);
    rest(book, 2, Side::Buy, 50, 2);
    rest(book, 3, Side::Buy, 1, 2);
    auto trades = book.addOrder(marketOrder(4, Side::Sell, 5));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 3u);
    expectTrade(trades->at(0), 1, 4, 100, 2);
    expectTrade(trades->at(1), 2, 4, 50, 2);
    expectTrade(trades->at(2), 3, 4, 1, 1);
    EXPECT_EQ(book.getBid(), 1);   // 1 left of order 3
    EXPECT_FALSE(book.getAsk());
    EXPECT_TRUE(book.validate());
}

// Market order that is fully filled leaves the rest of the book intact
TEST(OrderbookTest, MarketPartialLevel) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 10);
    auto trades = book.addOrder(marketOrder(2, Side::Buy, 4));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), 2, 1, 100, 4);
    EXPECT_EQ(book.getAsk(), 100);
    EXPECT_TRUE(book.cancelOrder(1));   // remaining 6 still cancellable
}

// ---------------------------------------------------------------------------
// Cancels
// ---------------------------------------------------------------------------

// Cancel one of two orders at a price: level stays
TEST(OrderbookTest, CancelKeepsLevel) {
    Orderbook book;
    book.addOrder(limitOrder(1, Side::Sell, 100, 5));
    book.addOrder(limitOrder(2, Side::Sell, 100, 5));
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_EQ(book.getAsk(), 100);
    auto trades = book.addOrder(limitOrder(3, Side::Buy, 100, 5));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), 3, 2, 100, 5);   // order 1 is gone
    EXPECT_TRUE(book.validate());
}

// Cancelling the last order at the best level moves the best price
TEST(OrderbookTest, CancelLastAtBestBid) {
    Orderbook book;
    rest(book, 1, Side::Buy, 100, 5);
    rest(book, 2, Side::Buy, 99, 5);
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_EQ(book.getBid(), 99);
    EXPECT_TRUE(book.cancelOrder(2));
    EXPECT_FALSE(book.getBid());
    EXPECT_TRUE(book.validate());
}

// Cancelling a middle order at a level keeps FIFO for the rest
TEST(OrderbookTest, CancelMiddleKeepsFifo) {
    Orderbook book;
    rest(book, 1, Side::Buy, 100, 1);
    rest(book, 2, Side::Buy, 100, 1);
    rest(book, 3, Side::Buy, 100, 1);
    EXPECT_TRUE(book.cancelOrder(2));
    auto trades = book.addOrder(limitOrder(4, Side::Sell, 100, 2));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 2u);
    expectTrade(trades->at(0), 1, 4, 100, 1);
    expectTrade(trades->at(1), 3, 4, 100, 1);
}

// Cancelling a partially filled order removes only what is left
TEST(OrderbookTest, CancelPartiallyFilled) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 10);
    rest(book, 2, Side::Sell, 100, 10);
    ASSERT_TRUE(book.addOrder(limitOrder(3, Side::Buy, 100, 4)).has_value());
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_TRUE(book.validate());   // level total must drop by 6, not 10
    auto trades = book.addOrder(marketOrder(4, Side::Buy, 100));
    ASSERT_TRUE(trades.has_value());
    ASSERT_EQ(trades->size(), 1u);
    expectTrade(trades->at(0), 4, 2, 100, 10);
}

// Fully filled orders can't be cancelled
TEST(OrderbookTest, CancelFilled) {
    Orderbook book;
    book.addOrder(limitOrder(1, Side::Sell, 100, 5));
    book.addOrder(limitOrder(2, Side::Buy, 100, 5));
    EXPECT_FALSE(book.cancelOrder(1));   // resting order filled
    EXPECT_FALSE(book.cancelOrder(2));   // incoming order never rested
    EXPECT_TRUE(book.validate());
}

TEST(OrderbookTest, CancelTwice) {
    Orderbook book;
    rest(book, 1, Side::Sell, 100, 5);
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.cancelOrder(1));
    EXPECT_FALSE(book.getAsk());
}

TEST(OrderbookTest, CancelUnknownLeavesBookAlone) {
    Orderbook book;
    rest(book, 1, Side::Buy, 100, 5);
    rest(book, 2, Side::Sell, 101, 5);
    EXPECT_FALSE(book.cancelOrder(42));
    EXPECT_EQ(book.getBid(), 100);
    EXPECT_EQ(book.getAsk(), 101);
    EXPECT_TRUE(book.validate());
}

// Rejected orders never enter the book, so they can't be cancelled
TEST(OrderbookTest, CancelRejected) {
    Orderbook book;
    ASSERT_FALSE(book.addOrder(limitOrder(1, Side::Buy, 100, 0)).has_value());
    EXPECT_FALSE(book.cancelOrder(1));
}
