#include <benchmark/benchmark.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>
#include "orderbook.h"



// swtich to debug mode: cmake -DCMAKE_BUILD_TYPE=Release .. (inside build directory)
// cmake --build build && ./build/build

using namespace me; 
// Seeded random order flow around a mid price: most orders rest, some cross, 5% are market orders.
// Same seed gives the same orders every time.
std::vector<Order> makeOrders(size_t numOrders, uint64_t seed = 42) {
    constexpr Price mid = 10'000;
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> percent(0, 99);
    std::uniform_int_distribution<Price> offset(-5, 20);   // distance from mid, negative = crosses
    std::uniform_int_distribution<Quantity> quantity(1, 100);

    std::vector<Order> orders;
    orders.reserve(numOrders);
    for (OrderId id = 1; id <= numOrders; ++id) {
        Side side = percent(rng) < 50 ? Side::Buy : Side::Sell;
        Price price = side == Side::Buy ? mid - offset(rng) : mid + offset(rng);
        OrderType type = percent(rng) < 5 ? OrderType::Market : OrderType::Limit;
        orders.push_back({.id = id, .price = price, .quantity = quantity(rng), .side = side, .type = type});
    }
    return orders;
}

// Feeds every order into a fresh book; every 4th add also cancels the order placed
// 100 adds earlier (it may already be filled). Returns the number of trades.
size_t runOrderFlow(const std::vector<Order>& orders) {
    constexpr size_t cancelLag = 100;
    Orderbook book;
    size_t trades = 0;
    for (size_t i = 0; i < orders.size(); ++i) {
        trades += book.addOrder(orders[i]).size();
        if (i % 4 == 0 && i >= cancelLag) book.cancelOrder(orders[i - cancelLag].id);
    }
    return trades;
}

void Benchmark(benchmark::State& state) {
    auto orders = makeOrders(1000000); 
    for (auto _ : state) {
        runOrderFlow(orders); 
    }
}

BENCHMARK(Benchmark); 

BENCHMARK_MAIN();