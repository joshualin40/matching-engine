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
            
            std::expected<std::vector<Trade>, RejectReason> addOrder(Order order) { // TODO: replace the commented out code by calling the helper function 
                if (order.quantity == 0) 
                    return std::unexpected(RejectReason::ZeroQuantity); 
                if (pool.isFull()) 
                    return std::unexpected(RejectReason::PoolFull); 
                if (lookup.contains(order.id)) 
                    return std::unexpected(RejectReason::DuplicateId); 

                std::vector<Trade> tradelist; 
                Price price = order.price;
                if (order.side == Side::Buy) { 
                    tradelist = match(order);
                    if (order.quantity == 0 || order.type == OrderType::Market) return tradelist; // no order to rest
                    else restOrder(order); 
                }
                else {
                    tradelist = match(order); 
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
                        bids[orderptr->price].remove(orderptr);
                        if (bids[orderptr->price].isEmpty()) bids.erase(orderptr->price); 
                    }
                    else { 
                        asks[orderptr->price].remove(orderptr);
                        if (asks[orderptr->price].isEmpty()) asks.erase(orderptr->price); 
                    }
                    lookup.erase(orderid); 
                    pool.deallocate(orderptr); 
                    return true; 
                }
                
                return false; 
            }
            
            std::optional<Price> getBid() const {
                if (!bids.empty()) return bids.begin()->first; // use  iterator to get last pricelevel
                
                return std::nullopt; 
            }
            std::optional<Price> getAsk() const {
                if (!asks.empty()) return asks.begin()->first; 
                
                return std::nullopt; 
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

            bool validate() const {
                for (auto it = asks.begin(); it != asks.end(); ++it) if (!(it->second.validate())) return false; 
                for (auto it = bids.begin(); it != bids.end(); ++it) if (!(it->second.validate())) return false; 
                return true; 
            }

        private:
            MemoryPool<Order, ORDER_CAPACITY> pool; 

            std::map<Price, PriceLevel> asks; // sorted list for asks. ascending (best ask is lowest)

            std::map<Price, PriceLevel, std::greater<Price>> bids; // sorted list for bids. descending (best bid is highest) 
            
            std::unordered_map<OrderId, Order*> lookup; // O(1) lookup

            std::vector<Trade> match(Order& order) { // refactor to only pass in order
                Price price = order.price; 
                std::vector<Trade> tradelist; 
                
                if (order.side == Side::Buy) {
                    while (!bids.empty() && (order.type == OrderType::Market || price > asks.begin()->first)) { // the comparator depends on comp 
                        PriceLevel& pricelevel = asks.begin()->second; // don't use this after if the pricelevel is to be deleted 
                        
                        Quantity decrement = std::min(order.quantity, pricelevel.head->quantity); 
                        order.quantity -= decrement; 
                        pricelevel.head->quantity -= decrement; 
                        pricelevel.totalquantity -= decrement; // decrement totalquantity of pricelevel here
                        
                        tradelist.push_back(Trade(order.id, pricelevel.head->id, pricelevel.head->price, decrement)); 
                        
                        if (pricelevel.head->quantity == 0) {
                            lookup.erase(pricelevel.head->id); 
                            Order* filled = pricelevel.head;
                            pricelevel.remove(filled);
                            pool.deallocate(filled);
                        }
                        if (pricelevel.isEmpty()) asks.erase(asks.begin()); 
                        if (order.quantity == 0) return tradelist; 
                    }
                }
                else {
                while (!bids.empty() && (order.type == OrderType::Market || price < bids.begin()->first)) { // the comparator depends on comp 
                        PriceLevel& pricelevel = bids.begin()->second; // don't use this after if the pricelevel is to be deleted 
                        
                        Quantity decrement = std::min(order.quantity, pricelevel.head->quantity); 
                        order.quantity -= decrement; 
                        pricelevel.head->quantity -= decrement; 
                        pricelevel.totalquantity -= decrement; // decrement totalquantity of pricelevel here
                       
                        tradelist.push_back(Trade(pricelevel.head->id, order.id, pricelevel.head->price, decrement)); 

                        if (pricelevel.head->quantity == 0) {
                            lookup.erase(pricelevel.head->id); 
                            Order* filled = pricelevel.head;
                            pricelevel.remove(filled);
                            pool.deallocate(filled);
                        }
                        if (pricelevel.isEmpty()) bids.erase(bids.begin()); 
                        if (order.quantity == 0) return tradelist; 
                    }
                }
                return tradelist; 
            }
    

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
