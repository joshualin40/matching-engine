#pragma once

#include <cstdint>
#include <list>
#include <print> 

namespace me
{
    using OrderId = uint64_t;
    using UserId = uint64_t;
    using Price = int64_t;
    using Quantity = uint32_t;

    constexpr Quantity ORDER_CAPACITY = 1'000;

    enum class Side : std::uint8_t
    {
        Buy,
        Sell
    };

    enum class OrderType : std::uint8_t
    {
        Limit,
        Market
    };

    enum class RejectReason
    { // enum for reasons an incoming Order may be rejected
        PoolFull,
        ZeroQuantity,
        DuplicateId
    };

    struct Order
    {
        Order(OrderId id_, 
            Price price_, 
            Quantity quantity_, 
            Side side_, 
            OrderType type_, 
            Order *next_ = nullptr, 
            Order *prev_ = nullptr) : 
            id(id_)
            , price(price_)
            , quantity(quantity_)
            , side(side_)
            , type(type_)
            , next(next_)
            , prev(prev_) {}

        OrderId id{};
        Price price{};
        Quantity quantity{};
        Side side{};
        OrderType type{};
        Order *next{nullptr};
        Order *prev{nullptr};
    };

    struct Trade
    {
        OrderId buyerid{};
        OrderId sellerid{};
        Price price{};
        Quantity quantity{};
    };

    struct PriceLevel
    {
        Order *head{nullptr};
        Order *tail{nullptr};
        Quantity totalquantity{};

        void append(Order *order)
        {
            if (std::numeric_limits<uint32_t>::max() - totalquantity < order->quantity)
                throw std::runtime_error("Not enough space for total quantity");
            totalquantity += order->quantity;
            if (isEmpty())
            {
                head = order;
                tail = order;
                return;
            }
            tail->next = order;
            order->prev = tail;
            tail = order;
        }

        void remove(Order* order)
        { // precondition: order is in price level
            if (!order)
                return;
            totalquantity -= order->quantity;

            if (order == head && order == tail) // Only one order in the list
            {
                head = nullptr;
                tail = nullptr;
                return;
            }

            if (order == head)
            {
                head = order->next;
                if (head)
                    head->prev = nullptr;
                return;
            }

            if (order == tail)
            {
                tail = order->prev;
                if (tail)
                    tail->next = nullptr;
                return;
            }

            // Order is in the middle
            if (order->prev)
                order->prev->next = order->next;
            if (order->next)
                order->next->prev = order->prev;
        }

        // True if the list is internally consistent. O(n): debug/tests only.
        bool validate() const
        {
            if ((head && !tail) || (!head && tail))
                return false;
            if (!head) 
                return (totalquantity == 0);
            if (head->prev != nullptr || tail->next != nullptr)
                return false;

            Order *fast = head;
            Order *slow = head;
            while (fast && fast->next)
            {
                slow = slow->next;
                fast = fast->next->next;
                if (fast == slow)
                    return false; // cycle exists
            }

            Order *curr = head;             
            Quantity quantity_ = curr->quantity; 
            while (curr->next)
            {
                quantity_ += curr->next->quantity;
                if (curr->next->prev != curr)
                    return false;
                curr = curr->next;
            }
            if (quantity_ != totalquantity)
                return false;

            return true;
        }

        bool isEmpty() const { return head == nullptr; }
    };
};
