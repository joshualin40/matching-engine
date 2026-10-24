# mentino modern cpp 20 modules support 

# addition of memory pool
    # operator new is expensive 
    # prevent fragmentation 

    # only one thread can access free list memory at once 

    # the C++ memory allocator must speak to the OS kernel. OS does a syscall
        # the new operator takes an extra step because the CPU needs to switch into kernel mode, and back into user mode 
        # other issues include fragmentation -> heap becomes fragmented after many calls to new 

    # memory pool -> a bag of many fix sized slots of memory that can you take and give back 