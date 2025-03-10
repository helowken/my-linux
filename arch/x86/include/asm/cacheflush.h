#ifndef _ASM_X86_CACHEFLUSH_H
#define _ASM_X86_CACHEFLUSH_H

/* Keep includes the same across arches.  */
#include <linux/mm.h>

/* Caches aren't brain-dead on the intel. */
static inline void flush_cache_all(void) { }
static inline void flush_cache_mm(struct mm_struct *mm) { }
static inline void flush_cache_dup_mm(struct mm_struct *mm) { }
static inline void flush_cache_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end) { }
static inline void flush_cache_page(struct vm_area_struct *vma,
				    unsigned long vmaddr, unsigned long pfn) { }
static inline void flush_dcache_page(struct page *page) { }
static inline void flush_dcache_mmap_lock(struct address_space *mapping) { }
static inline void flush_dcache_mmap_unlock(struct address_space *mapping) { }
static inline void flush_icache_range(unsigned long start,
				      unsigned long end) { }
static inline void flush_icache_page(struct vm_area_struct *vma,
				     struct page *page) { }
static inline void flush_icache_user_range(struct vm_area_struct *vma,
					   struct page *page,
					   unsigned long addr,
					   unsigned long len) { }
static inline void flush_cache_vmap(unsigned long start, unsigned long end) { }
static inline void flush_cache_vunmap(unsigned long start,
				      unsigned long end) { }

static inline void copy_to_user_page(struct vm_area_struct *vma,
				     struct page *page, unsigned long vaddr,
				     void *dst, const void *src,
				     unsigned long len)
{
	memcpy(dst, src, len);
}

static inline void copy_from_user_page(struct vm_area_struct *vma,
				       struct page *page, unsigned long vaddr,
				       void *dst, const void *src,
				       unsigned long len)
{
	memcpy(dst, src, len);
}

#define PG_WC				PG_arch_1
PAGEFLAG(WC, WC)

#ifdef CONFIG_X86_PAT
/*
 * X86 PAT uses page flags WC and Uncached together to keep track of
 * memory type of pages that have backing page struct. X86 PAT supports 3
 * different memory types, _PAGE_CACHE_WB, _PAGE_CACHE_WC and
 * _PAGE_CACHE_UC_MINUS and fourth state where page's memory type has not
 * been changed from its default (value of -1 used to denote this).
 * Note we do not support _PAGE_CACHE_UC here.
 *
 * Caller must hold memtype_lock for atomicity.
 */
static inline unsigned long get_page_memtype(struct page *pg)
{
	if (!PageUncached(pg) && !PageWC(pg))
		return -1;
	else if (!PageUncached(pg) && PageWC(pg))
		return _PAGE_CACHE_WC;
	else if (PageUncached(pg) && !PageWC(pg))
		return _PAGE_CACHE_UC_MINUS;
	else
		return _PAGE_CACHE_WB;
}

static inline void set_page_memtype(struct page *pg, unsigned long memtype)
{
	switch (memtype) {
	case _PAGE_CACHE_WC:
		ClearPageUncached(pg);
		SetPageWC(pg);
		break;
	case _PAGE_CACHE_UC_MINUS:
		SetPageUncached(pg);
		ClearPageWC(pg);
		break;
	case _PAGE_CACHE_WB:
		SetPageUncached(pg);
		SetPageWC(pg);
		break;
	default:
	case -1:
		ClearPageUncached(pg);
		ClearPageWC(pg);
		break;
	}
}
#else
//static inline unsigned long get_page_memtype(struct page *pg) { return -1; }
//static inline void set_page_memtype(struct page *pg, unsigned long memtype) { }
#endif

/*
 * The set_memory_* API can be used to change various attributes of a virtual
 * address range. The attributes include:
 * Cachability   : UnCached, WriteCombining, WriteBack
 * Executability : eXeutable, NoteXecutable
 * Read/Write    : ReadOnly, ReadWrite
 * Presence      : NotPresent
 *
 * Within a catagory, the attributes are mutually exclusive.
 *
 * The implementation of this API will take care of various aspects that
 * are associated with changing such attributes, such as:
 * - Flushing TLBs
 * - Flushing CPU caches
 * - Making sure aliases of the memory behind the mapping don't violate
 *   coherency rules as defined by the CPU in the system.
 *
 * What this API does not do:
 * - Provide exclusion between various callers - including callers that
 *   operation on other mappings of the same physical page
 * - Restore default attributes when a page is freed
 * - Guarantee that mappings other than the requested one are
 *   in any state, other than that these do not violate rules for
 *   the CPU you have. Do not depend on any effects on other mappings,
 *   CPUs other than the one you have may have more relaxed rules.
 * The caller is required to take care of these.
 */

int _set_memory_uc(unsigned long addr, int numpages);
int _set_memory_wc(unsigned long addr, int numpages);
int _set_memory_wb(unsigned long addr, int numpages);
int set_memory_uc(unsigned long addr, int numpages);
int set_memory_wc(unsigned long addr, int numpages);
int set_memory_wb(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_np(unsigned long addr, int numpages);
int set_memory_4k(unsigned long addr, int numpages);

int set_memory_array_uc(unsigned long *addr, int addrinarray);
int set_memory_array_wb(unsigned long *addr, int addrinarray);

int set_pages_array_uc(struct page **pages, int addrinarray);
int set_pages_array_wb(struct page **pages, int addrinarray);

/*
 * For legacy compatibility with the old APIs, a few functions
 * are provided that work on a "struct page".
 * These functions operate ONLY on the 1:1 kernel mapping of the
 * memory that the struct page represents, and internally just
 * call the set_memory_* function. See the description of the
 * set_memory_* function for more details on conventions.
 *
 * These APIs should be considered *deprecated* and are likely going to
 * be removed in the future.
 * The reason for this is the implicit operation on the 1:1 mapping only,
 * making this not a generally useful API.
 *
 * Specifically, many users of the old APIs had a virtual address,
 * called virt_to_page() or vmalloc_to_page() on that address to
 * get a struct page* that the old API required.
 * To convert these cases, use set_memory_*() on the original
 * virtual address, do not use these functions.
 */

int set_pages_uc(struct page *page, int numpages);
int set_pages_wb(struct page *page, int numpages);
int set_pages_x(struct page *page, int numpages);
int set_pages_nx(struct page *page, int numpages);
int set_pages_ro(struct page *page, int numpages);
int set_pages_rw(struct page *page, int numpages);


void clflush_cache_range(void *addr, unsigned int size);

#ifdef CONFIG_DEBUG_RODATA
void mark_rodata_ro(void);
extern const int rodata_test_data;
void set_kernel_text_rw(void);
void set_kernel_text_ro(void);
#else
//static inline void set_kernel_text_rw(void) { }
//static inline void set_kernel_text_ro(void) { }
#endif

#ifdef CONFIG_DEBUG_RODATA_TEST
int rodata_test(void);
#else
/*static inline int rodata_test(void)
{
	return 0;
}*/
#endif

#endif /* _ASM_X86_CACHEFLUSH_H */
