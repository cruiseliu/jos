#ifndef JOS_KERN_BUDDY_H
#define JOS_KERN_BUDDY_H

// Lowest 5 bits represents max free space under this node, in "log2 + 1" form
// i.e. 0 for 0, 1 for 1, 2 for 4, 3 for 8, 4 for 16, 5 for 32, etc.
// Higher bits represents the reference count, only 11 bits (0~2047) available
// as uint16_t. Use uint32_t or even uint64_t if you needs more.
typedef uint16_t bnode_t;

struct Buddy {
    uint32_t size;
    bnode_t tree[1]; // maybe we can use "tree[0]" or "tree[]"
};

#define OUT_OF_MEM          ~0
#define ADDR_UNAVAIL        ~1

#define IS_POWER_OF_2(x)    (!((x)&((x)-1)))

#define LEFT_CHILD(x)       ((x)*2+1)
#define RIGHT_CHILD(x)      ((x)*2+2)
#define PARENT(x)           (((x)-1)/2)

#define SIZE_OF_BUDDY(size) (((size)*2-1)*sizeof(bnode_t) + sizeof(uint32_t))

// Free space of a node
#define BUDDY_NODE_SIZE(x)  ((x)&0x1f ? 1<<(((x)&0x1f)-1) : 0)

// Physical address to node index
#define PA2NODE(b,pa)       (((pa)>>PGSHIFT)+(b)->size-1)

//#define BUDDY_INC_REF(b,pa) (b)->tree[PA2NODE((b),(pa))] += 0x20

#define BUDDY_DEC_REF(b,pa) (b)->tree[PA2NODE((b),(pa))] -= 0x20

#define BUDDY_GET_REF(b,pa) ((b)->tree[PA2NODE((b),(pa))] >> 5)

// Set reference count to 0, used by checkers
#define BUDDY_CLR_REF(b,pa) ((b)->tree[PA2NODE((b),(pa))] &= 0x1f)

void BUDDY_INC_REF(struct Buddy *b, uint32_t pa)
{
    assert(BUDDY_GET_REF(b, pa) <= 2000); // use uint32_t for bnode_t if overflow
    b->tree[PA2NODE(b, pa)] += 0x20;
}

static inline void buddy_update(struct Buddy *b, uint32_t node)
{
    bnode_t l = (b->tree[node * 2 + 1]) & 0x1f;
    bnode_t r = (b->tree[node * 2 + 2]) & 0x1f;
    b->tree[node] &= ~0x1f;
    b->tree[node] |= l > r ? l : r;
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

#endif
