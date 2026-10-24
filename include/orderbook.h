#pragma once

#include <map>
#include <functional>
#include <unordered_map> 
#include <optional> 
#include <vector> 
#include <algorithm>
#include <iomanip> 
#include <expected> 
#include <iostream> 
#include <memory> 

#include "types.h"
#include "memory_pool.h"

namespace me {
    class Orderbook {
        public:
            Orderbook() = default;
            // add order
            
            std::expected<std::vector<Trade>, RejectReason> addOrder(Order order) { // TODO: replace the commented out code by calling the helper function 
                if (order.quantity == 0) return std::unexpected(RejectReason::ZeroQuantity); 
                if (pool.isFull()) return std::unexpected(RejectReason::PoolFull); 

                std::vector<Trade> tradelist; 
                Price price = order.price;
                if (order.side == Side::Buy) { 
                    tradelist = match(asks, order);
                    if (order.quantity == 0 || order.type == OrderType::Market) return tradelist; // no order to rest
                    else restOrder(order); 
                }
                else {
                    tradelist = match(bids, order); 
                    if (order.quantity == 0 || order.type == OrderType::Market) return tradelist; // no order to rest
                    else restOrder(order); 
                } 
                
                return tradelist; 
            }

            bool cancelOrder(const OrderId orderid) { // TODO: replace the logic with helper function
                auto it = lookup.find(orderid); 
                if (it != lookup.end()) {
                    Order* orderptr = it->second; 
                    if (orderptr->side == Side::Buy) {
                        bids[orderptr->price].totalquantity -= orderptr->quantity; 
                        bids[orderptr->price].remove(orderptr);
                        if (bids[orderptr->price].isEmpty()) bids.erase(orderptr->price); 
                    }
                    else { 
                        asks[orderptr->price].totalquantity -= orderptr->quantity; 
                        asks[orderptr->price].remove(orderptr);
                        if (asks[orderptr->price].isEmpty()) asks.erase(orderptr->price); 
                    }
                    lookup.erase(orderid); 
                    pool.deallocate(orderptr); 
                    return true; 
                }
                else return false; 
            }

            std::optional<Price> getBid() const {
                if (!bids.empty()) return bids.begin()->first; // use  iterator to get last pricelevel
                else return std::nullopt; 
            }
            std::optional<Price> getAsk() const {
                if (!asks.empty()) return asks.begin()->first; 
                else return std::nullopt; 
            }

            void getDepth(size_t depth) const {
                std::cout << std::setw(10) << "ASKS\n"; 
                auto stop = std::next(asks.begin(), std::min(depth, asks.size()));
                for (auto it = std::make_reverse_iterator(stop); it != asks.rend(); ++it)
                    std::cout << "$" << it->first << ": " << it->second.totalquantity << "\n";
                std::cout << std::setw(10) << "BIDS\n"; 
                for (auto it = bids.begin(); it != std::next(bids.begin(), std::min(depth,bids.size())); it++) 
                    std::cout << "$" << it->first << ": " << it->second.totalquantity << "\n"; 
            }

        private:
            // TODO: implement an intrusive list, such that each order carries its own prev/next pointers, orders live in a preallocated pool, and the lookup maps id to pointer
            // (Optimize)
            MemoryPool<Order, ORDER_CAPACITY> pool; 

            std::map<Price, PriceLevel> asks; // sorted list for asks. ascending (best ask is lowest)

            std::map<Price, PriceLevel, std::greater<Price>> bids; // sorted list for bids. descending (best bid is highest) 
            
            std::unordered_map<OrderId, Order*> lookup; 
        
            // Takes order and its opposite side and does all the matching 
            // returns a list of all Trade's that occured 
            template <typename T> 
            std::vector<Trade> match(T& matchmap, Order& order) {
                Price price = order.price; 
                std::vector<Trade> tradelist; 
                auto comp = matchmap.key_comp(); // comp(a,b) returns true if a comes before b in the map ordering 
                while (!matchmap.empty() && (order.type == OrderType::Market || !comp(price, matchmap.begin()->first))) { // the comparator depends on comp 
                        auto& pricelevel = matchmap.begin()->second; // don't use this after if the pricelevel is to be deleted 
                        Quantity decrement = std::min(order.quantity, pricelevel.head->quantity); 
                        order.quantity -= decrement; 
                        pricelevel.head->quantity -= decrement; 
                        pricelevel.totalquantity -= decrement; // decrement totalquantity of pricelevel here
                        if (order.side == Side::Buy) tradelist.push_back(Trade(order.id, pricelevel.head->id, pricelevel.head->price, decrement)); 
                        else tradelist.push_back(Trade(pricelevel.head->id, order.id, pricelevel.head->price, decrement)); 
                        if (pricelevel.head->quantity == 0) {
                            lookup.erase(pricelevel.head->id); 
                            pool.deallocate(pricelevel.head); 
                            pricelevel.remove(pricelevel.head);
                        }
                        if (pricelevel.isEmpty()) matchmap.erase(matchmap.begin()); 
                        if (order.quantity == 0) return tradelist; 
                    }
                
                return tradelist; 
            }
    
            // Helper function to help add incoming resting Orders to the side's level at its price, and add it to the lookup
            // returns pointer to the slot 
            Order* restOrder(Order order) {
                Order* slot = pool.allocate(); 
                if (slot == nullptr) return nullptr; // not enough space

                slot = std::construct_at(slot, order); 
                if (slot->side == Side::Buy) bids[slot->price].append(slot);
                else                         asks[slot->price].append(slot);

                lookup[slot->id] = slot; 
                return slot; 
            }
        };

}
