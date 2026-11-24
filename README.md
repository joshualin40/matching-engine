# Matching Engine

A limit order book and matching engine written in C++23. 

Work in Progress: The core matching logic works and is covered by tests. I'm currently refactoring the code, especially `Orderbook`, to make it more readable and to follow design principles such as SOLID.

## Features

- Limit and market orders
- Order cancellation by ID in O(1) lookup
- Price-time priority matching (best price first, then oldest order first)
- Fixed-capacity memory pool, so the hot path does no heap allocation for orders
- Rejection of bad orders with clear reasons (`ZeroQuantity`, `DuplicateId`, `PoolFull`) through `std::expected`
- Debug invariant checks for every price level
- Unit tests with GoogleTest and a benchmark with Google Benchmark

## Project Structure

```
include/
  types.h          Core types: Order, Trade, PriceLevel, Side, OrderType, RejectReason
  memory_pool.h    Fixed-size object pool with an intrusive free list
  orderbook.h      Orderbook: add, cancel, match, best bid/ask, depth
src/
  test_orderbook.cpp   GoogleTest tests for Orderbook
  test_pricelevel.cpp  GoogleTest tests for PriceLevel
  benchmark.cpp        Google Benchmark order-flow benchmark
external/
  googletest/      Vendored GoogleTest
```

## How It Works

### The order book

The book holds two sides, each a sorted map from price to a `PriceLevel`:

```cpp
std::map<Price, PriceLevel>                      asks; // ascending: best ask = lowest price
std::map<Price, PriceLevel, std::greater<Price>> bids; // descending: best bid = highest price
std::unordered_map<OrderId, Order*>              lookup;
```

Each `PriceLevel` is a doubly linked list of the orders resting at that price, in the order they arrived, plus a running `totalquantity` for that level.

### Adding an order

When `addOrder()` receives an order, it:

1. **Validates** the order. A zero quantity, a duplicate ID or a full pool returns `std::unexpected(RejectReason)`.
2. **Matches** it against the opposite side. A buy walks the asks from lowest price up, and a sell walks the bids from highest price down. At each level it fills against the **head** order (the oldest one), creates a `Trade` at the resting order's price, and removes orders and price levels once they are empty.
3. **Stops** when the incoming order is filled, the book side is empty, or (for a limit order) the next price no longer crosses.
4. **Rests** any remaining quantity of a limit order in the book. It takes a slot from the memory pool, appends the order to the tail of its price level, and records it in `lookup`. Leftover market order quantity is dropped rather than rested.

It returns the list of trades produced.

### Cancelling an order

`cancelOrder()` finds the order through `lookup`, unlinks it from its price level, erases the level if it is now empty, and returns the slot to the pool.

## Design Decisions

**`std::map` for price levels.**
Matching always needs the best price, and the depth view needs prices in order. A sorted map gives the best price at `begin()` and keeps levels ordered. Inserting and erasing a level costs O(log n), but there are usually far fewer price levels than orders, so this stays cheap. Using `std::greater` for bids means both sides read "best price first" from `begin()`, which allows one templated `match()` function to serve both sides. It compares prices with the map's own `key_comp()`.

**Intrusive doubly linked list per price level.**
Each `Order` stores its own `next` and `prev` pointers. This gives:
- O(1) append at the tail, which preserves time priority
- O(1) access to the head, the next order to fill
- O(1) removal from anywhere in the list, which is what makes cancellation fast

Althought an `std::list` would be notably easier to use, letting us forgo implementing our own doubly linked list, it would need a separate allocation per node or would not support O(1) removal from the middle by pointer.

**`unordered_map<OrderId, Order*>` for lookup.**
Cancelling by ID should not need to search the book. The hash map takes the ID straight to the order's node, and the intrusive list then unlinks it in O(1).

**Custom memory pool.**
`MemoryPool<T, N>` allocates all `N` slots once at construction and keeps free slots in an intrusive free list (the first 8 bytes of a free slot store the pointer to the next free slot). Allocating and freeing are each a single pointer swap with no system call and no fragmentation. Slots are aligned to at least 64 bytes so two orders never share a cache line. The pool is non-copyable and non-movable, because orders in the book point into its storage.

**AddressSanitizer poisoning.**
Freed slots are marked as poisoned with `ASAN_POISON_MEMORY_REGION`. Under an ASan build, any read or write to an order after it has been returned to the pool fails immediately rather than silently reading stale data. This is how the use-after-free bug in the bug log below was caught.

**`std::expected` for rejections.**
Rejecting an order is normal business logic, not an exceptional event. `std::expected<std::vector<Trade>, RejectReason>` makes the caller handle both outcomes and avoids the cost of exceptions on the hot path.

**Invariant checks.**
`PriceLevel::validate()` checks that the list is consistent: head and tail agree, there are no cycles, every `prev`/`next` pair matches, every order has the level's price, and the quantities add up to `totalquantity`. `Orderbook::validate()` runs it for every level. These checks are O(n) and meant for tests and debugging only.

**Integer prices.**
`Price` is an `int64_t` (in ticks) rather than a floating-point number, so price comparisons are exact.

## Building and Testing

Requirements: a C++23 compiler, CMake 3.28+, and [Google Benchmark](https://github.com/google/benchmark) installed. GoogleTest is vendored in `external/`.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

To build with AddressSanitizer so the pool poisoning is enforced:

```bash
cmake -S . -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
cmake --build build-asan
ctest --test-dir build-asan
```

### Benchmark

`src/benchmark.cpp` generates 1,000,000 seeded random orders around a mid price (most rest, some cross, 5% are market orders) and cancels an earlier order on every 4th add. Build it in Release mode for meaningful numbers.

## Bug Log

### Earlier orders unreachable after cancelling a tail order

**Symptom:** After cancelling the tail order at a price level, the orders before it could no longer be reached.

**Causes:**
- In `PriceLevel::append()`, `tail` was reassigned before `order->prev` was set, so `order->prev` pointed at the order itself. Removing a tail order then failed to reset the tail pointer correctly.
- In `Orderbook::match()`, a filled order was returned to the pool *before* `remove()` unlinked it. `remove()` then read `order->next` from memory the pool already owned and might have overwritten.

**Fixes:**
- Set `order->prev` before reassigning `tail` in `PriceLevel::append()`.
- Unlink filled orders before returning them to the pool.
- Added invariant validation methods to `PriceLevel` and `Orderbook`.
- Added AddressSanitizer poisoning to pool memory to catch use-after-free bugs.
