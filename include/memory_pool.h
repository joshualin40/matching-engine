#pragma once 

#include <array> 
#include <cstddef> 
#include <memory> 

namespace me {
    template <typename T, std::size_t N> 
    class MemoryPool {
        static_assert(N > 1, "Error: Must allocate more than 1 slot"); 
        static_assert(sizeof(T) >= 8U, "Error: Size of the allocated object must be more than 1 byte");

        struct alignas((alignof(T) > 64U) ? alignof(T) : 64U) Slot {  // alignas as the max of 64 bytes and T
            std::array<std::byte, sizeof(T)> storage{};
        };

        struct FreeNode {
            FreeNode* next; 
        };

        public: 
            MemoryPool() { // reserves N chunks of memory, marking every Slot as free 
                slots = std::make_unique<Slot[]>(N);
                FreeNode* curr = nullptr; 
                for (size_t i = 0; i < N- 1; i++) {
                    curr = reinterpret_cast<FreeNode*>(slots[i].storage.data()); // reinterpret_cast: tells compiler to treat sequence of bits in memory as a different type 
                    curr->next = reinterpret_cast<FreeNode*>(slots[i+1].storage.data()); // .data(): returns  a pointer to the first elemetn of the underlying array 
                } // we chained the slots together, writing the address of the next FreeNode* into the first 8 bytes of each slot 
                curr->next->next = nullptr; 
                freehead = reinterpret_cast<FreeNode*>(slots[0].storage.data()); 
            }
            
            // delete copy/move assignment and copy to avoid UB 
            MemoryPool(const MemoryPool& copy) = delete; // delete copy constructor 
            MemoryPool operator=(const MemoryPool& other) = delete; // delete assignment operator
            MemoryPool(MemoryPool&& other) = delete; // delete move constructor
            MemoryPool &operator=(MemoryPool&& other) = delete; // delete move assignment 

            

            T* allocate() noexcept { // hands out one slot of memory 
                if (freehead == nullptr) return nullptr; // nothing to allocate
                FreeNode* node = freehead; 
                freehead = freehead->next;
                allocated_count++; 
                return reinterpret_cast<T*>(node); 
            }   
            
            void deallocate(T* object) noexcept { // gives slot back to pool
                if (object == nullptr) [[unlikely]] {
                    return;
                }
                std::destroy_at(object); // call object destructor without freeing its memory 
                auto* node = reinterpret_cast<FreeNode*>(object); 
                node->next = freehead; 
                freehead = node; 
                allocated_count--; 
            }   

            bool isFull() const {
                return (freehead == nullptr); 
            }

        
        private: 
            std::unique_ptr<Slot[]> slots; 

            FreeNode* freehead {}; 

            size_t allocated_count {}; 
    };

}