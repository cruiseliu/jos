/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_PMAP_H
#define JOS_KERN_PMAP_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/memlayout.h>
#include <inc/assert.h>
#include <kern/settings.h>

struct Env;

extern char bootstacktop[], bootstack[];

extern PageInfo *pages;

extern size_t npages;

#ifdef USE_BUDDY
extern size_t buddy_size;
#endif

extern pde_t *kern_pgdir;

/* This macro takes a kernel virtual address -- an address that points above
 * KERNBASE, where the machine's maximum 256MB of physical memory is mapped --
 * and returns the corresponding physical address.  It panics if you pass it a
 * non-kernel virtual address.
 */
#define PADDR(kva) _paddr(__FILE__, __LINE__, kva)

static inline physaddr_t
_paddr(const char *file, int line, void *kva)
{
	if ((uint32_t)kva < KERNBASE)
		_panic(file, line, "PADDR called with invalid kva %08lx", kva);
	return (physaddr_t)kva - KERNBASE;
}

/* This macro takes a physical address and returns the corresponding kernel
 * virtual address.  It panics if you pass an invalid physical address. */
#define KADDR(pa) _kaddr(__FILE__, __LINE__, pa)

static inline void*
_kaddr(const char *file, int line, physaddr_t pa)
{
	if (PGNUM(pa) >= npages)
		_panic(file, line, "KADDR called with invalid pa %08lx", pa);
	return (void *)(pa + KERNBASE);
}


enum {
	// For page_alloc, zero the returned physical page.
	ALLOC_ZERO = 1<<0,
};

void	mem_init(void);

void	page_init(void);

PageInfo *page_alloc(int alloc_flags);
void	page_free(PageInfo *pp);
int	page_insert(pde_t *pgdir, PageInfo *pp, void *va, int perm);
void	page_remove(pde_t *pgdir, void *va);
PageInfo *page_lookup(pde_t *pgdir, void *va, pte_t **pte_store);
void	page_decref(PageInfo *pp);

void	tlb_invalidate(pde_t *pgdir, void *va);

int	user_mem_check(struct Env *env, const void *va, size_t len, int perm);
void	user_mem_assert(struct Env *env, const void *va, size_t len, int perm);

#ifdef USE_BUDDY

physaddr_t kmalloc(size_t size);
physaddr_t kcalloc(size_t size);
void kfree(physaddr_t pa);

#define SIZE_MASK           0x1f
#define REF_ONE             0x20    // SIZE_MASK + 1
#define REF_SHIFT           5       // log2(REF_ONE)

// FIXME: only available for single page
static inline physaddr_t page2pa(PageInfo *pp)
{
    return ((pp - pages) + 1 - buddy_size) << PGSHIFT;
}

static inline PageInfo* pa2page(physaddr_t pa)
{
    size_t idx = (pa >> PGSHIFT) + buddy_size - 1;
    //if (idx >= npages)
    if (idx >= buddy_size * 2)
        panic("pa2page called with invalid pa %08x (pages[%d])", pa, idx);
    return &pages[idx];
}

#else

static inline physaddr_t
page2pa(PageInfo *pp)
{
	return (pp - pages) << PGSHIFT;
}

static inline PageInfo*
pa2page(physaddr_t pa)
{
	if (PGNUM(pa) >= npages)
		panic("pa2page called with invalid pa");
	return &pages[PGNUM(pa)];
}

#endif

static inline int inc_ref(PageInfo *p)
{
#ifdef USE_BUDDY
    *p += REF_ONE;
    return *p >> REF_SHIFT;
#else
    return ++p->pp_ref;
#endif
}

static inline int dec_ref(PageInfo *p)
{
#ifdef USE_BUDDY
    *p -= REF_ONE;
    return *p >> REF_SHIFT;
#else
    return --p->pp_ref;
#endif
}

static inline int get_ref(PageInfo *p)
{
#ifdef USE_BUDDY
    return *p >> REF_SHIFT;
#else
    return p->pp_ref;
#endif
}

// used by checkers
static inline void clr_ref(PageInfo *p)
{
#ifdef USE_BUDDY
    *p &= SIZE_MASK;
#else
    p->pp_ref = 0;
#endif
}

static inline void*
page2kva(PageInfo *pp)
{
	return KADDR(page2pa(pp));
}

pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create);

int showmappings(pde_t *pgdir, uint32_t low, uint32_t high);
int setpage(uint32_t low, uint32_t high, const char *perm);
int memdump(uint32_t low, uint32_t size, bool phys);

#endif /* !JOS_KERN_PMAP_H */
