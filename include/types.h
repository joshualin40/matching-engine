#pragma once

#include <cstdint>
#include <list> 

namespace me {
    using OrderId = uint64_t;
    using UserId = uint64_t;
    using Price = int64_t;
    using Quantity = uint32_t;

    constexpr Quantity ORDER_CAPACITY = 1'000'000;
    
    enum class Side : std::uint8_t {
        Buy,
        Sell
    };

    enum class OrderType : std::uint8_t {
        Limit, 
        Market
    }; 

    enum class RejectReason { // enum for reasons an incoming Order may be rejected 
        PoolFull, 
        ZeroQuantity
    }; 

    struct Order {
        OrderId     id{};
        Price       price{};
        Quantity    quantity{};
        Side        side{};
        OrderType   type{}; 
        Order* next{nullptr}; 
        Order* prev{nullptr}; 
    };

    struct Trade {
        OrderId  buyerid{};
        OrderId  sellerid{};
        Price    price{}; 
        Quantity quantity{};
    };

    struct PriceLevel {
        // std::list<Order>    orderlist{}; 
        Order*              head{nullptr}; 
        Order*              tail{nullptr};   
        Quantity            totalquantity{};
        
        void append(Order* order) { // precondition, Order is freshly constructed (at least next/prev and nullptrs) and appended once
            totalquantity += order->quantity; 
            if (isEmpty()) {
                head = order;
                tail = order; 
                return; 
            }
            tail->next = order; 
            tail = order; 
            order->prev = tail;
        }

        void remove(Order* order) { // precondition: order is in price level
            if (!order) return;

            if (order == head && order == tail) {
                // Only one order in the list
                head = nullptr;
                tail = nullptr;
                return;
            }

            if (order == head) {
                head = order->next;
                if (head) head->prev = nullptr;
                return;
            }

            if (order == tail) {
                tail = order->prev;
                if (tail) tail->next = nullptr;
                return;
            }

            // Order is in the middle
            if (order->prev) order->prev->next = order->next;
            if (order->next) order->next->prev = order->prev;
        }
        
        bool isEmpty() const { return head == nullptr; }
    };
}; 
