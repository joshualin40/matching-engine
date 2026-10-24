#include <cassert>
#include <cstdint>
#include <benchmark/benchmark.h>
#include <iostream>
#include <random>
#include <vector>
#include "orderbook.h"

using namespace me;


// swtich to debug mode: cmake -DCMAKE_BUILD_TYPE=Debug .. (inside build directory)
// cmake --build build && ./build/build


// Checks one trade's fields (buy id, sell id, price, quantity)
void checkTrade(const Trade& t, OrderId buy, OrderId sell, Price price, Quantity qty) {
    auto [b, s, p, q] = t;
    assert(b == buy && s == sell && p == price && q == qty);
}

// Builds a market order (price is ignored)
Order marketOrder(OrderId id, Side side, Quantity qty) {
    return {.id = id, .quantity = qty, .side = side, .type = OrderType::Market};
}

// Sweeps several levels, leftover rests at its own price
void testSweepLevels() {
    Orderbook book;
    book.addOrder({.id = 1, .price = 100, .quantity = 2, .side = Side::Sell, .type = OrderType::Limit});
    book.addOrder({.id = 2, .price = 101, .quantity = 2, .side = Side::Sell, .type = OrderType::Limit});
    book.addOrder({.id = 3, .price = 102, .quantity = 2, .side = Side::Sell, .type = OrderType::Limit});
    auto trades = book.addOrder({.id = 4, .price = 101, .quantity = 7, .side = Side::Buy, .type = OrderType::Limit});
    assert(trades.has_value()); 
    // trade is a wrapper, acting like a smart pointer so we have to dereference through -> 
    assert(trades->size() == 2);
    checkTrade(trades->at(0), 4, 1, 100, 2);
    checkTrade(trades->at(1), 4, 2, 101, 2);
    assert(book.getBid() == 101);   // leftover 3 rests
    assert(book.getAsk() == 102);   // 102 untouched
    std::cout << "testSweepLevels passed\n";
}

// Same price fills in arrival order
void testFifo() {
    Orderbook book;
    book.addOrder({.id = 1, .price = 100, .quantity = 3, .side = Side::Sell, .type = OrderType::Limit});
    book.addOrder({.id = 2, .price = 100, .quantity = 3, .side = Side::Sell, .type = OrderType::Limit});
    book.addOrder({.id = 3, .price = 100, .quantity = 3, .side = Side::Sell, .type = OrderType::Limit});
    auto trades = book.addOrder({.id = 4, .price = 100, .quantity = 5, .side = Side::Buy, .type = OrderType::Limit});
    assert(trades.has_value()); 
    assert(trades->size() == 2);
    checkTrade(trades->at(0), 4, 1, 100, 3);
    checkTrade(trades->at(1), 4, 2, 100, 2);
    std::cout << "testFifo passed\n";
}

// Sell side mirror: incoming sell hits bids, trades at the bid's price
void testSellHitsBids() {
    Orderbook book;
    book.addOrder({.id = 1, .price = 102, .quantity = 4, .side = Side::Buy, .type = OrderType::Limit});
    book.addOrder({.id = 2, .price = 101, .quantity = 4, .side = Side::Buy, .type = OrderType::Limit});
    auto trades = book.addOrder({.id = 3, .price = 100, .quantity = 6, .side = Side::Sell, .type = OrderType::Limit});
    assert(trades.has_value());
    assert(trades->size() == 2);
    checkTrade(trades->at(0), 1, 3, 102, 4);   // resting price, not 100
    checkTrade(trades->at(1), 2, 3, 101, 2);
    assert(book.getBid() == 101);
    assert(!book.getAsk());
    std::cout << "testSellHitsBids passed\n";
}

// Cancel one of two orders at a price: level stays
void testCancelKeepsLevel() {
    Orderbook book;
    book.addOrder({.id = 1, .price = 100, .quantity = 5, .side = Side::Sell, .type = OrderType::Limit});
    book.addOrder({.id = 2, .price = 100, .quantity = 5, .side = Side::Sell, .type = OrderType::Limit});
    assert(book.cancelOrder(1));
    assert(book.getAsk() == 100);
    auto trades = book.addOrder({.id = 3, .price = 100, .quantity = 5, .side = Side::Buy, .type = OrderType::Limit});
    assert(trades.has_value());
    assert(trades->size() == 1);
    checkTrade(trades->at(0), 3, 2, 100, 5);   // order 1 is gone
    std::cout << "testCancelKeepsLevel passed\n";
}

// Fully filled orders can't be cancelled
void testCancelFilled() {
    Orderbook book;
    book.addOrder({.id = 1, .price = 100, .quantity = 5, .side = Side::Sell, .type = OrderType::Limit});
    book.addOrder({.id = 2, .price = 100, .quantity = 5, .side = Side::Buy, .type = OrderType::Limit});
    assert(!book.cancelOrder(1));   // resting order filled
    assert(!book.cancelOrder(2));   // incoming order never rested
    std::cout << "testCancelFilled passed\n";
}

// Market order larger than the book: leftover is dropped
void testMarketRunsOut() {
    Orderbook book;
    book.addOrder({.id = 1, .price = 100, .quantity = 3, .side = Side::Sell, .type = OrderType::Limit});
    auto trades = book.addOrder(marketOrder(2, Side::Buy, 10));
    assert(trades.has_value());
    assert(trades->size() == 1);
    checkTrade(trades->at(0), 2, 1, 100, 3);
    assert(!book.getAsk());
    assert(!book.getBid());          // leftover 7 not rested
    assert(!book.cancelOrder(2));    // and not in the lookup
    std::cout << "testMarketRunsOut passed\n";
}

int main() {
    testSweepLevels();
    testFifo();
    testSellHitsBids();
    testCancelKeepsLevel();
    testCancelFilled();
    testMarketRunsOut();
    std::cout << "All tests passed\n";
}
