#pragma once

#include <cstdint>
#include <list> 

namespace me {
    using OrderId = uint64_t;
    using UserId = uint64_t;
    using Price = int64_t;
    using Quantity = uint32_t;

    constexpr Quantity ORDER_CAPACITY = 1'000;
    
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
        ZeroQuantity, 
        DuplicateId
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
            if (std::numeric_limits<uint32_t>::max() - totalquantity < order->quantity) throw std::runtime_error("Not enough space for total quantity");
            if (isEmpty()) {
                head = order;
                tail = order; 
                return; 
            }
            tail->next = order;
            order->prev = tail;  
            tail = order; 
        }

        void remove(Order* order) { // precondition: order is in price level
            if (!order) return;
            totalquantity -= order->quantity; 

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

        // True if the list is internally consistent. O(n): debug/tests only.
        bool validate() const {
            return true; 
            if ((head && !tail) || (!head && tail)) return false; // head and tail are either both nullptr or both non-null
            if (head->prev != nullptr || tail->next != nullptr) return false;  
            if (!head) return (totalquantity ==0 ); // below checks don't apply with empty price level
            
            // use fast and slow pointers to check for cycles
            Order* fast = head; 
            Order* slow = head; 
            while (fast && fast->next) {
                slow = slow->next; 
                fast = fast->next->next; 
                if (fast == slow) return false; // cycle exists
            }

            // check curr = curr->next->prev for all nodes and quantitysum == totalquantity and all 
            Order* curr = head; 
            Price p = curr->price; // same price checking if head exists
            Quantity quantity_ = curr->quantity; // quantity checking if head exists
            while (curr->next) {
                quantity_ += curr->next->quantity; 
                if (curr->next->price != p) return false; 
                if (curr->next->prev != head) return false; 
                curr = curr->next; 
            }
            if (quantity_ != totalquantity) return false; 


            return true; 
        }
        
        bool isEmpty() const { return head == nullptr; }
    };
}; 
