#ifndef JOS_KERN_BUDDY_H
#define JOS_KERN_BUDDY_H

#include <kern/pmap.h>

static inline void update_node(uint32_t node)
{
    PageInfo l = (pages[node * 2 + 1]) & SIZE_MASK;
    PageInfo r = (pages[node * 2 + 2]) & SIZE_MASK;
    pages[node] &= ~SIZE_MASK;
    pages[node] |= l > r ? l : r;
}

static inline void build_node(uint32_t node, PageInfo layer)
{
    PageInfo l = (pages[node * 2 + 1]) & SIZE_MASK;
    PageInfo r = (pages[node * 2 + 2]) & SIZE_MASK;

    if (l == layer - 1 && r == layer - 1)
        // Both children are free, merge them
        pages[node] = layer & SIZE_MASK;
    else
        pages[node] = l > r ? l : r;
}

static inline uint32_t up_to_power_of_2(uint32_t x)
{
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

#define IS_POWER_OF_2(x)    (!( (x) & ((x)-1) ))

#define BUDDY_NODE_SIZE(x)  ((x)&SIZE_MASK ? 1<<(((x)&SIZE_MASK)-1) : 0)

#define LEFT_CHILD(x)       ((x)*2+1)
#define RIGHT_CHILD(x)      ((x)*2+2)
#define PARENT(x)           (((x)-1)/2)

#endif
