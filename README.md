# Bug Log

- Issue: After cancelling a tail. Order at a price level, the elements before are inaccessible 
- Reason: in PriceLevel::append(), tail is reassigned before order->prev is set so order->prev points at itself. Then 
removing a tail order would not reassign the tail pointer properly. 
- Reason: in Orderbook::match(), it frees the filled Order back to the pool before remove() unlinked it, so remove() read order->next from memory the pool already owned and might have overwritten.
- Fix: 
    - set Order->prev before reassigning tail pointer in PriceLevel
    - added invariant validation methods in PriceLevel struct and Orderbook class
    - Added AddressSanitizer Poisoning to Pool memory to more strictly enforce memory usage


git 