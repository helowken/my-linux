/*
 * Memory merging support.
 *
 * This code enables dynamic sharing of identical pages found in different
 * memory areas, even if they are not shared by fork()
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.
 * Authors:
 *	Izik Eidus
 *	Andrea Arcangeli
 *	Chris Wright
 *	Hugh Dickins
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/rwsem.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/spinlock.h>
#include <linux/jhash.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/mmu_notifier.h>
#include <linux/swap.h>
#include <linux/ksm.h>

#include <asm/tlbflush.h>
#include "internal.h"

/*
 * A few notes about the KSM scanning process,
 * to make it easier to understand the data structures below:
 *
 * In order to reduce excessive scanning, KSM sorts the memory pages by their
 * contents into a data structure that holds pointers to the pages' locations.
 *
 * Since the contents of the pages may change at any moment, KSM cannot just
 * insert the pages into a normal sorted tree and expect it to find anything.
 * Therefore KSM uses two data structures - the stable and the unstable tree.
 *
 * The stable tree holds pointers to all the merged pages (ksm pages), sorted
 * by their contents.  Because each such page is write-protected, searching on
 * this tree is fully assured to be working (except when pages are unmapped),
 * and therefore this tree is called the stable tree.
 *
 * In addition to the stable tree, KSM uses a second data structure called the
 * unstable tree: this tree holds pointers to pages which have been found to
 * be "unchanged for a period of time".  The unstable tree sorts these pages
 * by their contents, but since they are not write-protected, KSM cannot rely
 * upon the unstable tree to work correctly - the unstable tree is liable to
 * be corrupted as its contents are modified, and so it is called unstable.
 *
 * KSM solves this problem by several techniques:
 *
 * 1) The unstable tree is flushed every time KSM completes scanning all
 *    memory areas, and then the tree is rebuilt again from the beginning.
 * 2) KSM will only insert into the unstable tree, pages whose hash value
 *    has not changed since the previous scan of all memory areas.
 * 3) The unstable tree is a RedBlack Tree - so its balancing is based on the
 *    colors of the nodes and not on their contents, assuring that even when
 *    the tree gets "corrupted" it won't get out of balance, so scanning time
 *    remains the same (also, searching and inserting nodes in an rbtree uses
 *    the same algorithm, so we have no overhead when we flush and rebuild).
 * 4) KSM never flushes the stable tree, which means that even if it were to
 *    take 10 attempts to find a page in the unstable tree, once it is found,
 *    it is secured in the stable tree.  (When we scan a new page, we first
 *    compare it against the stable tree, and then against the unstable tree.)
 */

/**
 * struct mm_slot - ksm information per mm that is being scanned
 * @link: link to the mm_slots hash list
 * @mm_list: link into the mm_slots list, rooted in ksm_mm_head
 * @rmap_list: head for this mm_slot's list of rmap_items
 * @mm: the mm that this information is valid for
 */
struct mm_slot {
	struct hlist_node link;
	struct list_head mm_list;
	struct list_head rmap_list;
	struct mm_struct *mm;
};

/**
 * struct ksm_scan - cursor for scanning
 * @mm_slot: the current mm_slot we are scanning
 * @address: the next address inside that to be scanned
 * @rmap_item: the current rmap that we are scanning inside the rmap_list
 * @seqnr: count of completed full scans (needed when removing unstable node)
 *
 * There is only the one ksm_scan instance of this cursor structure.
 */
struct ksm_scan {
	struct mm_slot *mm_slot;
	unsigned long address;
	struct rmap_item *rmap_item;
	unsigned long seqnr;
};

/**
 * struct rmap_item - reverse mapping item for virtual addresses
 * @link: link into mm_slot's rmap_list (rmap_list is per mm)
 * @mm: the memory structure this rmap_item is pointing into
 * @address: the virtual address this rmap_item tracks (+ flags in low bits)
 * @oldchecksum: previous checksum of the page at that virtual address
 * @node: rb_node of this rmap_item in either unstable or stable tree
 * @next: next rmap_item hanging off the same node of the stable tree
 * @prev: previous rmap_item hanging off the same node of the stable tree
 */
struct rmap_item {
	struct list_head link;
	struct mm_struct *mm;
	unsigned long address;		/* + low bits used for flags below */
	union {
		unsigned int oldchecksum;		/* when unstable */
		struct rmap_item *next;			/* when stable */
	};
	union {
		struct rb_node node;			/* when tree node */
		struct rmap_item *prev;			/* in stable list */
	};
};

#define SEQNR_MASK	0x0ff	/* low bits of unstable tree seqnr */
#define NODE_FLAG	0x100	/* is a node of unstable or stable tree */
#define STABLE_FLAG	0x200	/* is a node or list item of stable tree */

/* The stable and unstable tree heads */
static struct rb_root root_stable_tree = RB_ROOT;
static struct rb_root root_unstable_tree = RB_ROOT;

#define MM_SLOTS_HASH_HEADS 1024
static struct hlist_head *mm_slots_hash;

static struct mm_slot ksm_mm_head = {
	.mm_list = LIST_HEAD_INIT(ksm_mm_head.mm_list),
};
static struct ksm_scan ksm_scan = {
	.mm_slot = &ksm_mm_head,
};

static struct kmem_cache *rmap_item_cache;
static struct kmem_cache *mm_slot_cache;

/* The number of nodes in the stable tree */
static unsigned long ksm_pages_shared;

/* The number of page slots additionally sharing those nodes */
static unsigned long ksm_pages_sharing;

/* The number of nodes in the unstable tree */
static unsigned long ksm_pages_unshared;

/* The number of rmap_items in use: to calculate pages_volatile */
static unsigned long ksm_rmap_items;

/* Limit on the number of unswappable pages used */
static unsigned long ksm_max_kernel_pages;

/* Number of pages ksmd should scan in one batch */
static unsigned int ksm_thread_pages_to_scan = 100;

/* Milliseconds ksmd should sleep between batches */
static unsigned int ksm_thread_sleep_millisecs = 20;

#define KSM_RUN_STOP	0
#define KSM_RUN_MERGE	1
#define KSM_RUN_UNMERGE	2
static unsigned int ksm_run = KSM_RUN_STOP;

static DECLARE_WAIT_QUEUE_HEAD(ksm_thread_wait);
static DEFINE_MUTEX(ksm_thread_mutex);
static DEFINE_SPINLOCK(ksm_mmlist_lock);

#define KSM_KMEM_CACHE(__struct, __flags) kmem_cache_create("ksm_"#__struct,\
		sizeof(struct __struct), __alignof__(struct __struct),\
		(__flags), NULL)

static int __init ksm_slab_init(void)
{
	rmap_item_cache = KSM_KMEM_CACHE(rmap_item, 0);
	if (!rmap_item_cache)
		goto out;

	mm_slot_cache = KSM_KMEM_CACHE(mm_slot, 0);
	if (!mm_slot_cache)
		goto out_free;

	return 0;

out_free:
	kmem_cache_destroy(rmap_item_cache);
out:
	return -ENOMEM;
}

static void __init ksm_slab_free(void)
{
	kmem_cache_destroy(mm_slot_cache);
	kmem_cache_destroy(rmap_item_cache);
	mm_slot_cache = NULL;
}

static inline struct rmap_item *alloc_rmap_item(void)
{
	struct rmap_item *rmap_item;

	rmap_item = kmem_cache_zalloc(rmap_item_cache, GFP_KERNEL);
	if (rmap_item)
		ksm_rmap_items++;
	return rmap_item;
}

static inline void free_rmap_item(struct rmap_item *rmap_item)
{
	ksm_rmap_items--;
	rmap_item->mm = NULL;	/* debug safety */
	kmem_cache_free(rmap_item_cache, rmap_item);
}

static inline struct mm_slot *alloc_mm_slot(void)
{
	if (!mm_slot_cache)	/* initialization failed */
		return NULL;
	return kmem_cache_zalloc(mm_slot_cache, GFP_KERNEL);
}

static inline void free_mm_slot(struct mm_slot *mm_slot)
{
	kmem_cache_free(mm_slot_cache, mm_slot);
}

static int __init mm_slots_hash_init(void)
{
	mm_slots_hash = kzalloc(MM_SLOTS_HASH_HEADS * sizeof(struct hlist_head),
				GFP_KERNEL);
	if (!mm_slots_hash)
		return -ENOMEM;
	return 0;
}

static void __init mm_slots_hash_free(void)
{
	kfree(mm_slots_hash);
}

static struct mm_slot *get_mm_slot(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	struct hlist_head *bucket;
	struct hlist_node *node;

	bucket = &mm_slots_hash[((unsigned long)mm / sizeof(struct mm_struct))
				% MM_SLOTS_HASH_HEADS];
	hlist_for_each_entry(mm_slot, node, bucket, link) {
		if (mm == mm_slot->mm)
			return mm_slot;
	}
	return NULL;
}

static void insert_to_mm_slots_hash(struct mm_struct *mm,
				    struct mm_slot *mm_slot)
{
	struct hlist_head *bucket;

	bucket = &mm_slots_hash[((unsigned long)mm / sizeof(struct mm_struct))
				% MM_SLOTS_HASH_HEADS];
	mm_slot->mm = mm;
	INIT_LIST_HEAD(&mm_slot->rmap_list);
	hlist_add_head(&mm_slot->link, bucket);
}

static inline int in_stable_tree(struct rmap_item *rmap_item)
{
	return rmap_item->address & STABLE_FLAG;
}

/*
 * ksmd, and unmerge_and_remove_all_rmap_items(), must not touch an mm's
 * page tables after it has passed through ksm_exit() - which, if necessary,
 * takes mmap_sem briefly to serialize against them.  ksm_exit() does not set
 * a special flag: they can just back out as soon as mm_users goes to zero.
 * ksm_test_exit() is used throughout to make this test for exit: in some
 * places for correctness, in some places just to avoid unnecessary work.
 */
static inline bool ksm_test_exit(struct mm_struct *mm)
{
	return atomic_read(&mm->mm_users) == 0;
}

/*
 * We use break_ksm to break COW on a ksm page: it's a stripped down
 *
 *	if (get_user_pages(current, mm, addr, 1, 1, 1, &page, NULL) == 1)
 *		put_page(page);
 *
 * but taking great care only to touch a ksm page, in a VM_MERGEABLE vma,
 * in case the application has unmapped and remapped mm,addr meanwhile.
 * Could a ksm page appear anywhere else?  Actually yes, in a VM_PFNMAP
 * mmap of /dev/mem or /dev/kmem, where we would not want to touch it.
 */
static int break_ksm(struct vm_area_struct *vma, unsigned long addr)
{
	struct page *page;
	int ret = 0;

	do {
		cond_resched();
		page = follow_page(vma, addr, FOLL_GET);
		if (!page)
			break;
		if (PageKsm(page))
			ret = handle_mm_fault(vma->vm_mm, vma, addr,
							FAULT_FLAG_WRITE);
		else
			ret = VM_FAULT_WRITE;
		put_page(page);
	} while (!(ret & (VM_FAULT_WRITE | VM_FAULT_SIGBUS | VM_FAULT_OOM)));
	/*
	 * We must loop because handle_mm_fault() may back out if there's
	 * any difficulty e.g. if pte accessed bit gets updated concurrently.
	 *
	 * VM_FAULT_WRITE is what we have been hoping for: it indicates that
	 * COW has been broken, even if the vma does not permit VM_WRITE;
	 * but note that a concurrent fault might break PageKsm for us.
	 *
	 * VM_FAULT_SIGBUS could occur if we race with truncation of the
	 * backing file, which also invalidates anonymous pages: that's
	 * okay, that truncation will have unmapped the PageKsm for us.
	 *
	 * VM_FAULT_OOM: at the time of writing (late July 2009), setting
	 * aside mem_cgroup limits, VM_FAULT_OOM would only be set if the
	 * current task has TIF_MEMDIE set, and will be OOM killed on return
	 * to user; and ksmd, having no mm, would never be chosen for that.
	 *
	 * But if the mm is in a limited mem_cgroup, then the fault may fail
	 * with VM_FAULT_OOM even if the current task is not TIF_MEMDIE; and
	 * even ksmd can fail in this way - though it's usually breaking ksm
	 * just to undo a merge it made a moment before, so unlikely to oom.
	 *
	 * That's a pity: we might therefore have more kernel pages allocated
	 * than we're counting as nodes in the stable tree; but ksm_do_scan
	 * will retry to break_cow on each pass, so should recover the page
	 * in due course.  The important thing is to not let VM_MERGEABLE
	 * be cleared while any such pages might remain in the area.
	 */
	return (ret & VM_FAULT_OOM) ? -ENOMEM : 0;
}

static void break_cow(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma;

	down_read(&mm->mmap_sem);
	if (ksm_test_exit(mm))
		goto out;
	vma = find_vma(mm, addr);
	if (!vma || vma->vm_start > addr)
		goto out;
	if (!(vma->vm_flags & VM_MERGEABLE) || !vma->anon_vma)
		goto out;
	break_ksm(vma, addr);
out:
	up_read(&mm->mmap_sem);
}

static struct page *get_mergeable_page(struct rmap_item *rmap_item)
{
	struct mm_struct *mm = rmap_item->mm;
	unsigned long addr = rmap_item->address;
	struct vm_area_struct *vma;
	struct page *page;

	down_read(&mm->mmap_sem);
	if (ksm_test_exit(mm))
		goto out;
	vma = find_vma(mm, addr);
	if (!vma || vma->vm_start > addr)
		goto out;
	if (!(vma->vm_flags & VM_MERGEABLE) || !vma->anon_vma)
		goto out;

	page = follow_page(vma, addr, FOLL_GET);
	if (!page)
		goto out;
	if (PageAnon(page)) {
		flush_anon_page(vma, page, addr);
		flush_dcache_page(page);
	} else {
		put_page(page);
out:		page = NULL;
	}
	up_read(&mm->mmap_sem);
	return page;
}

/*
 * get_ksm_page: checks if the page at the virtual address in rmap_item
 * is still PageKsm, in which case we can trust the content of the page,
 * and it returns the gotten page; but NULL if the page has been zapped.
 */
static struct page *get_ksm_page(struct rmap_item *rmap_item)
{
	struct page *page;

	page = get_mergeable_page(rmap_item);
	if (page && !PageKsm(page)) {
		put_page(page);
		page = NULL;
	}
	return page;
}

/*
 * Removing rmap_item from stable or unstable tree.
 * This function will clean the information from the stable/unstable tree.
 */
static void remove_rmap_item_from_tree(struct rmap_item *rmap_item)
{
	if (in_stable_tree(rmap_item)) {
		struct rmap_item *next_item = rmap_item->next;

		if (rmap_item->address & NODE_FLAG) {
			if (next_item) {
				rb_replace_node(&rmap_item->node,
						&next_item->node,
						&root_stable_tree);
				next_item->address |= NODE_FLAG;
				ksm_pages_sharing--;
			} else {
				rb_erase(&rmap_item->node, &root_stable_tree);
				ksm_pages_shared--;
			}
		} else {
			struct rmap_item *prev_item = rmap_item->prev;

			BUG_ON(prev_item->next != rmap_item);
			prev_item->next = next_item;
			if (next_item) {
				BUG_ON(next_item->prev != rmap_item);
				next_item->prev = rmap_item->prev;
			}
			ksm_pages_sharing--;
		}

		rmap_item->next = NULL;

	} else if (rmap_item->address & NODE_FLAG) {
		unsigned char age;
		/*
		 * Usually ksmd can and must skip the rb_erase, because
		 * root_unstable_tree was already reset to RB_ROOT.
		 * But be careful when an mm is exiting: do the rb_erase
		 * if this rmap_item was inserted by this scan, rather
		 * than left over from before.
		 */
		age = (unsigned char)(ksm_scan.seqnr - rmap_item->address);
		BUG_ON(age > 1);
		if (!age)
			rb_erase(&rmap_item->node, &root_unstable_tree);
		ksm_pages_unshared--;
	}

	rmap_item->address &= PAGE_MASK;

	cond_resched();		/* we're called from many long loops */
}

static void remove_trailing_rmap_items(struct mm_slot *mm_slot,
				       struct list_head *cur)
{
	struct rmap_item *rmap_item;

	while (cur != &mm_slot->rmap_list) {
		rmap_item = list_entry(cur, struct rmap_item, link);
		cur = cur->next;
		remove_rmap_item_from_tree(rmap_item);
		list_del(&rmap_item->link);
		free_rmap_item(rmap_item);
	}
}

/*
 * Though it's very tempting to unmerge in_stable_tree(rmap_item)s rather
 * than check every pte of a given vma, the locking doesn't quite work for
 * that - an rmap_item is assigned to the stable tree after inserting ksm
 * page and upping mmap_sem.  Nor does it fit with the way we skip dup'ing
 * rmap_items from parent to child at fork time (so as not to waste time
 * if exit comes before the next scan reaches it).
 *
 * Similarly, although we'd like to remove rmap_items (so updating counts
 * and freeing memory) when unmerging an area, it's easier to leave that
 * to the next pass of ksmd - consider, for example, how ksmd might be
 * in cmp_and_merge_page on one of the rmap_items we would be removing.
 */
static int unmerge_ksm_pages(struct vm_area_struct *vma,
			     unsigned long start, unsigned long end)
{
	unsigned long addr;
	int err = 0;

	for (addr = start; addr < end && !err; addr += PAGE_SIZE) {
		if (ksm_test_exit(vma->vm_mm))
			break;
		if (signal_pending(current))
			err = -ERESTARTSYS;
		else
			err = break_ksm(vma, addr);
	}
	return err;
}

#ifdef CONFIG_SYSFS
/*
 * Only called through the sysfs control interface:
 */
static int unmerge_and_remove_all_rmap_items(void)
{
	struct mm_slot *mm_slot;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	int err = 0;

	spin_lock(&ksm_mmlist_lock);
	ksm_scan.mm_slot = list_entry(ksm_mm_head.mm_list.next,
						struct mm_slot, mm_list);
	spin_unlock(&ksm_mmlist_lock);

	for (mm_slot = ksm_scan.mm_slot;
			mm_slot != &ksm_mm_head; mm_slot = ksm_scan.mm_slot) {
		mm = mm_slot->mm;
		down_read(&mm->mmap_sem);
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			if (ksm_test_exit(mm))
				break;
			if (!(vma->vm_flags & VM_MERGEABLE) || !vma->anon_vma)
				continue;
			err = unmerge_ksm_pages(vma,
						vma->vm_start, vma->vm_end);
			if (err)
				goto error;
		}

		remove_trailing_rmap_items(mm_slot, mm_slot->rmap_list.next);

		spin_lock(&ksm_mmlist_lock);
		ksm_scan.mm_slot = list_entry(mm_slot->mm_list.next,
						struct mm_slot, mm_list);
		if (ksm_test_exit(mm)) {
			hlist_del(&mm_slot->link);
			list_del(&mm_slot->mm_list);
			spin_unlock(&ksm_mmlist_lock);

			free_mm_slot(mm_slot);
			clear_bit(MMF_VM_MERGEABLE, &mm->flags);
			up_read(&mm->mmap_sem);
			mmdrop(mm);
		} else {
			spin_unlock(&ksm_mmlist_lock);
			up_read(&mm->mmap_sem);
		}
	}

	ksm_scan.seqnr = 0;
	return 0;

error:
	up_read(&mm->mmap_sem);
	spin_lock(&ksm_mmlist_lock);
	ksm_scan.mm_slot = &ksm_mm_head;
	spin_unlock(&ksm_mmlist_lock);
	return err;
}
#endif /* CONFIG_SYSFS */

static u32 calc_checksum(struct page *page)
{
	u32 checksum;
	void *addr = kmap_atomic(page, KM_USER0);
	checksum = jhash2(addr, PAGE_SIZE / 4, 17);
	kunmap_atomic(addr, KM_USER0);
	return checksum;
}

static int memcmp_pages(struct page *page1, struct page *page2)
{
	char *addr1, *addr2;
	int ret;

	addr1 = kmap_atomic(page1, KM_USER0);
	addr2 = kmap_atomic(page2, KM_USER1);
	ret = memcmp(addr1, addr2, PAGE_SIZE);
	kunmap_atomic(addr2, KM_USER1);
	kunmap_atomic(addr1, KM_USER0);
	return ret;
}

static inline int pages_identical(struct page *page1, struct page *page2)
{
	return !memcmp_pages(page1, page2);
}

static int write_protect_page(struct vm_area_struct *vma, struct page *page,
			      pte_t *orig_pte)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr;
	pte_t *ptep;
	spinlock_t *ptl;
	int swapped;
	int err = -EFAULT;

	addr = page_address_in_vma(page, vma);
	if (addr == -EFAULT)
		goto out;

	ptep = page_check_address(page, mm, addr, &ptl, 0);
	if (!ptep)
		goto out;

	if (pte_write(*ptep)) {
		pte_t entry;

		swapped = PageSwapCache(page);
		flush_cache_page(vma, addr, page_to_pfn(page));
		/*
		 * Ok this is tricky, when get_user_pages_fast() run it doesnt
		 * take any lock, therefore the check that we are going to make
		 * with the pagecount against the mapcount is racey and
		 * O_DIRECT can happen right after the check.
		 * So we clear the pte and flush the tlb before the check
		 * this assure us that no O_DIRECT can happen after the check
		 * or in the middle of the check.
		 */
		entry = ptep_clear_flush(vma, addr, ptep);
		/*
		 * Check that no O_DIRECT or similar I/O is in progress on the
		 * page
		 */
		if ((page_mapcount(page) + 2 + swapped) != page_count(page)) {
			set_pte_at_notify(mm, addr, ptep, entry);
			goto out_unlock;
		}
		entry = pte_wrprotect(entry);
		set_pte_at_notify(mm, addr, ptep, entry);
	}
	*orig_pte = *ptep;
	err = 0;

out_unlock:
	pte_unmap_unlock(ptep, ptl);
out:
	return err;
}

/**
 * replace_page - replace page in vma by new ksm page
 * @vma:      vma that holds the pte pointing to oldpage
 * @oldpage:  the page we are replacing by newpage
 * @newpage:  the ksm page we replace oldpage by
 * @orig_pte: the original value of the pte
 *
 * Returns 0 on success, -EFAULT on failure.
 */
static int replace_page(struct vm_area_struct *vma, struct page *oldpage,
			struct page *newpage, pte_t orig_pte)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;
	spinlock_t *ptl;
	unsigned long addr;
	pgprot_t prot;
	int err = -EFAULT;

	prot = vm_get_page_prot(vma->vm_flags & ~VM_WRITE);

	addr = page_address_in_vma(oldpage, vma);
	if (addr == -EFAULT)
		goto out;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		goto out;

	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		goto out;

	ptep = pte_offset_map_lock(mm, pmd, addr, &ptl);
	if (!pte_same(*ptep, orig_pte)) {
		pte_unmap_unlock(ptep, ptl);
		goto out;
	}

	get_page(newpage);
	page_add_ksm_rmap(newpage);

	flush_cache_page(vma, addr, pte_pfn(*ptep));
	ptep_clear_flush(vma, addr, ptep);
	set_pte_at_notify(mm, addr, ptep, mk_pte(newpage, prot));

	page_remove_rmap(oldpage);
	put_page(oldpage);

	pte_unmap_unlock(ptep, ptl);
	err = 0;
out:
	return err;
}

/*
 * try_to_merge_one_page - take two pages and merge them into one
 * @vma: the vma that hold the pte pointing into oldpage
 * @oldpage: the page that we want to replace with newpage
 * @newpage: the page that we want to map instead of oldpage
 *
 * Note:
 * oldpage should be a PageAnon page, while newpage should be a PageKsm page,
 * or a newly allocated kernel page which page_add_ksm_rmap will make PageKsm.
 *
 * This function returns 0 if the pages were merged, -EFAULT otherwise.
 */
static int try_to_merge_one_page(struct vm_area_struct *vma,
				 struct page *oldpage,
				 struct page *newpage)
{
	pte_t orig_pte = __pte(0);
	int err = -EFAULT;

	if (!(vma->vm_flags & VM_MERGEABLE))
		goto out;

	if (!PageAnon(oldpage))
		goto out;

	get_page(newpage);
	get_page(oldpage);

	/*
	 * We need the page lock to read a stable PageSwapCache in
	 * write_protect_page().  We use trylock_page() instead of
	 * lock_page() because we don't want to wait here - we
	 * prefer to continue scanning and merging different pages,
	 * then come back to this page when it is unlocked.
	 */
	if (!trylock_page(oldpage))
		goto out_putpage;
	/*
	 * If this anonymous page is mapped only here, its pte may need
	 * to be write-protected.  If it's mapped elsewhere, all of its
	 * ptes are necessarily already write-protected.  But in either
	 * case, we need to lock and check page_count is not raised.
	 */
	if (write_protect_page(vma, oldpage, &orig_pte) == 0 &&
	    pages_identical(oldpage, newpage))
		err = replace_page(vma, oldpage, newpage, orig_pte);

	if ((vma->vm_flags & VM_LOCKED) && !err)
		munlock_vma_page(oldpage);

	unlock_page(oldpage);
out_putpage:
	put_page(oldpage);
	put_page(newpage);
out:
	return err;
}

/*
 * try_to_merge_with_ksm_page - like try_to_merge_two_pages,
 * but no new kernel page is allocated: kpage must already be a ksm page.
 */
static int try_to_merge_with_ksm_page(struct mm_struct *mm1,
				      unsigned long addr1,
				      struct page *page1,
				      struct page *kpage)
{
	struct vm_area_struct *vma;
	int err = -EFAULT;

	down_read(&mm1->mmap_sem);
	if (ksm_test_exit(mm1))
		goto out;

	vma = find_vma(mm1, addr1);
	if (!vma || vma->vm_start > addr1)
		goto out;

	err = try_to_merge_one_page(vma, page1, kpage);
out:
	up_read(&mm1->mmap_sem);
	return err;
}

/*
 * try_to_merge_two_pages - take two identical pages and prepare them
 * to be merged into one page.
 *
 * This function returns 0 if we successfully mapped two identical pages
 * into one page, -EFAULT otherwise.
 *
 * Note that this function allocates a new kernel page: if one of the pages
 * is already a ksm page, try_to_merge_with_ksm_page should be used.
 */
static int try_to_merge_two_pages(struct mm_struct *mm1, unsigned long addr1,
				  struct page *page1, struct mm_struct *mm2,
				  unsigned long addr2, struct page *page2)
{
	struct vm_area_struct *vma;
	struct page *kpage;
	int err = -EFAULT;

	/*
	 * The number of nodes in the stable tree
	 * is the number of kernel pages that we hold.
	 */
	if (ksm_max_kernel_pages &&
	    ksm_max_kernel_pages <= ksm_pages_shared)
		return err;

	kpage = alloc_page(GFP_HIGHUSER);
	if (!kpage)
		return err;

	down_read(&mm1->mmap_sem);
	if (ksm_test_exit(mm1)) {
		up_read(&mm1->mmap_sem);
		goto out;
	}
	vma = find_vma(mm1, addr1);
	if (!vma || vma->vm_start > addr1) {
		up_read(&mm1->mmap_sem);
		goto out;
	}

	copy_user_highpage(kpage, page1, addr1, vma);
	err = try_to_merge_one_page(vma, page1, kpage);
	up_read(&mm1->mmap_sem);

	if (!err) {
		err = try_to_merge_with_ksm_page(mm2, addr2, page2, kpage);
		/*
		 * If that fails, we have a ksm page with only one pte
		 * pointing to it: so break it.
		 */
		if (err)
			break_cow(mm1, addr1);
	}
out:
	put_page(kpage);
	return err;
}

/*
 * stable_tree_search - search page inside the stable tree
 * @page: the page that we are searching identical pages to.
 * @page2: pointer into identical page that we are holding inside the stable
 *	   tree that we have found.
 * @rmap_item: the reverse mapping item
 *
 * This function checks if there is a page inside the stable tree
 * with identical content to the page that we are scanning right now.
 *
 * This function return rmap_item pointer to the identical item if found,
 * NULL otherwise.
 */
static struct rmap_item *stable_tree_search(struct page *page,
					    struct page **page2,
					    struct rmap_item *rmap_item)
{
	struct rb_node *node = root_stable_tree.rb_node;

	while (node) {
		struct rmap_item *tree_rmap_item, *next_rmap_item;
		int ret;

		tree_rmap_item = rb_entry(node, struct rmap_item, node);
		while (tree_rmap_item) {
			BUG_ON(!in_stable_tree(tree_rmap_item));
			cond_resched();
			page2[0] = get_ksm_page(tree_rmap_item);
			if (page2[0])
				break;
			next_rmap_item = tree_rmap_item->next;
			remove_rmap_item_from_tree(tree_rmap_item);
			tree_rmap_item = next_rmap_item;
		}
		if (!tree_rmap_item)
			return NULL;

		ret = memcmp_pages(page, page2[0]);

		if (ret < 0) {
			put_page(page2[0]);
			node = node->rb_left;
		} else if (ret > 0) {
			put_page(page2[0]);
			node = node->rb_right;
		} else {
			return tree_rmap_item;
		}
	}

	return NULL;
}

/*
 * stable_tree_insert - insert rmap_item pointing to new ksm page
 * into the stable tree.
 *
 * @page: the page that we are searching identical page to inside the stable
 *	  tree.
 * @rmap_item: pointer to the reverse mapping item.
 *
 * This function returns rmap_item if success, NULL otherwise.
 */
static struct rmap_item *stable_tree_insert(struct page *page,
					    struct rmap_item *rmap_item)
{
	struct rb_node **new = &root_stable_tree.rb_node;
	struct rb_node *parent = NULL;

	while (*new) {
		struct rmap_item *tree_rmap_item, *next_rmap_item;
		struct page *tree_page;
		int ret;

		tree_rmap_item = rb_entry(*new, struct rmap_item, node);
		while (tree_rmap_item) {
			BUG_ON(!in_stable_tree(tree_rmap_item));
			cond_resched();
			tree_page = get_ksm_page(tree_rmap_item);
			if (tree_page)
				break;
			next_rmap_item = tree_rmap_item->next;
			remove_rmap_item_from_tree(tree_rmap_item);
			tree_rmap_item = next_rmap_item;
		}
		if (!tree_rmap_item)
			return NULL;

		ret = memcmp_pages(page, tree_page);
		put_page(tree_page);

		parent = *new;
		if (ret < 0)
			new = &parent->rb_left;
		else if (ret > 0)
			new = &parent->rb_right;
		else {
			/*
			 * It is not a bug that stable_tree_search() didn't
			 * find this node: because at that time our page was
			 * not yet write-protected, so may have changed since.
			 */
			return NULL;
		}
	}

	rmap_item->address |= NODE_FLAG | STABLE_FLAG;
	rmap_item->next = NULL;
	rb_link_node(&rmap_item->node, parent, new);
	rb_insert_color(&rmap_item->node, &root_stable_tree);

	ksm_pages_shared++;
	return rmap_item;
}

/*
 * unstable_tree_search_insert - search and insert items into the unstable tree.
 *
 * @page: the page that we are going to search for identical page or to insert
 *	  into the unstable tree
 * @page2: pointer into identical page that was found inside the unstable tree
 * @rmap_item: the reverse mapping item of page
 *
 * This function searches for a page in the unstable tree identical to the
 * page currently being scanned; and if no identical page is found in the
 * tree, we insert rmap_item as a new object into the unstable tree.
 *
 * This function returns pointer to rmap_item found to be identical
 * to the currently scanned page, NULL otherwise.
 *
 * This function does both searching and inserting, because they share
 * the same walking algorithm in an rbtree.
 */
static struct rmap_item *unstable_tree_search_insert(struct page *page,
						struct page **page2,
						struct rmap_item *rmap_item)
{
	struct rb_node **new = &root_unstable_tree.rb_node;
	struct rb_node *parent = NULL;

	while (*new) {
		struct rmap_item *tree_rmap_item;
		int ret;

		cond_resched();
		tree_rmap_item = rb_entry(*new, struct rmap_item, node);
		page2[0] = get_mergeable_page(tree_rmap_item);
		if (!page2[0])
			return NULL;

		/*
		 * Don't substitute an unswappable ksm page
		 * just for one good swappable forked page.
		 */
		if (page == page2[0]) {
			put_page(page2[0]);
			return NULL;
		}

		ret = memcmp_pages(page, page2[0]);

		parent = *new;
		if (ret < 0) {
			put_page(page2[0]);
			new = &parent->rb_left;
		} else if (ret > 0) {
			put_page(page2[0]);
			new = &parent->rb_right;
		} else {
			return tree_rmap_item;
		}
	}

	rmap_item->address |= NODE_FLAG;
	rmap_item->address |= (ksm_scan.seqnr & SEQNR_MASK);
	rb_link_node(&rmap_item->node, parent, new);
	rb_insert_color(&rmap_item->node, &root_unstable_tree);

	ksm_pages_unshared++;
	return NULL;
}

/*
 * stable_tree_append - add another rmap_item to the linked list of
 * rmap_items hanging off a given node of the stable tree, all sharing
 * the same ksm page.
 */
static void stable_tree_append(struct rmap_item *rmap_item,
			       struct rmap_item *tree_rmap_item)
{
	rmap_item->next = tree_rmap_item->next;
	rmap_item->prev = tree_rmap_item;

	if (tree_rmap_item->next)
		tree_rmap_item->next->prev = rmap_item;

	tree_rmap_item->next = rmap_item;
	rmap_item->address |= STABLE_FLAG;

	ksm_pages_sharing++;
}

/*
 * cmp_and_merge_page - first see if page can be merged into the stable tree;
 * if not, compare checksum to previous and if it's the same, see if page can
 * be inserted into the unstable tree, or merged with a page already there and
 * both transferred to the stable tree.
 *
 * @page: the page that we are searching identical page to.
 * @rmap_item: the reverse mapping into the virtual address of this page
 */
static void cmp_and_merge_page(struct page *page, struct rmap_item *rmap_item)
{
	struct page *page2[1];
	struct rmap_item *tree_rmap_item;
	unsigned int checksum;
	int err;

	if (in_stable_tree(rmap_item))
		remove_rmap_item_from_tree(rmap_item);

	/* We first start with searching the page inside the stable tree */
	tree_rmap_item = stable_tree_search(page, page2, rmap_item);
	if (tree_rmap_item) {
		if (page == page2[0])			/* forked */
			err = 0;
		else
			err = try_to_merge_with_ksm_page(rmap_item->mm,
							 rmap_item->address,
							 page, page2[0]);
		put_page(page2[0]);

		if (!err) {
			/*
			 * The page was successfully merged:
			 * add its rmap_item to the stable tree.
			 */
			stable_tree_append(rmap_item, tree_rmap_item);
		}
		return;
	}

	/*
	 * A ksm page might have got here by fork, but its other
	 * references have already been removed from the stable tree.
	 * Or it might be left over from a break_ksm which failed
	 * when the mem_cgroup had reached its limit: try again now.
	 */
	if (PageKsm(page))
		break_cow(rmap_item->mm, rmap_item->address);

	/*
	 * In case the hash value of the page was changed from the last time we
	 * have calculated it, this page to be changed frequely, therefore we
	 * don't want to insert it to the unstable tree, and we don't want to
	 * waste our time to search if there is something identical to it there.
	 */
	checksum = calc_checksum(page);
	if (rmap_item->oldchecksum != checksum) {
		rmap_item->oldchecksum = checksum;
		return;
	}

	tree_rmap_item = unstable_tree_search_insert(page, page2, rmap_item);
	if (tree_rmap_item) {
		err = try_to_merge_two_pages(rmap_item->mm,
					     rmap_item->address, page,
					     tree_rmap_item->mm,
					     tree_rmap_item->address, page2[0]);
		/*
		 * As soon as we merge this page, we want to remove the
		 * rmap_item of the page we have merged with from the unstable
		 * tree, and insert it instead as new node in the stable tree.
		 */
		if (!err) {
			rb_erase(&tree_rmap_item->node, &root_unstable_tree);
			tree_rmap_item->address &= ~NODE_FLAG;
			ksm_pages_unshared--;

			/*
			 * If we fail to insert the page into the stable tree,
			 * we will have 2 virtual addresses that are pointing
			 * to a ksm page left outside the stable tree,
			 * in which case we need to break_cow on both.
			 */
			if (stable_tree_insert(page2[0], tree_rmap_item))
				stable_tree_append(rmap_item, tree_rmap_item);
			else {
				break_cow(tree_rmap_item->mm,
						tree_rmap_item->address);
				break_cow(rmap_item->mm, rmap_item->address);
			}
		}

		put_page(page2[0]);
	}
}

static struct rmap_item *get_next_rmap_item(struct mm_slot *mm_slot,
					    struct list_head *cur,
					    unsigned long addr)
{
	struct rmap_item *rmap_item;

	while (cur != &mm_slot->rmap_list) {
		rmap_item = list_entry(cur, struct rmap_item, link);
		if ((rmap_item->address & PAGE_MASK) == addr) {
			if (!in_stable_tree(rmap_item))
				remove_rmap_item_from_tree(rmap_item);
			return rmap_item;
		}
		if (rmap_item->address > addr)
			break;
		cur = cur->next;
		remove_rmap_item_from_tree(rmap_item);
		list_del(&rmap_item->link);
		free_rmap_item(rmap_item);
	}

	rmap_item = alloc_rmap_item();
	if (rmap_item) {
		/* It has already been zeroed */
		rmap_item->mm = mm_slot->mm;
		rmap_item->address = addr;
		list_add_tail(&rmap_item->link, cur);
	}
	return rmap_item;
}

static struct rmap_item *scan_get_next_rmap_item(struct page **page)
{
	struct mm_struct *mm;
	struct mm_slot *slot;
	struct vm_area_struct *vma;
	struct rmap_item *rmap_item;

	if (list_empty(&ksm_mm_head.mm_list))
		return NULL;

	slot = ksm_scan.mm_slot;
	if (slot == &ksm_mm_head) {
		root_unstable_tree = RB_ROOT;

		spin_lock(&ksm_mmlist_lock);
		slot = list_entry(slot->mm_list.next, struct mm_slot, mm_list);
		ksm_scan.mm_slot = slot;
		spin_unlock(&ksm_mmlist_lock);
next_mm:
		ksm_scan.address = 0;
		ksm_scan.rmap_item = list_entry(&slot->rmap_list,
						struct rmap_item, link);
	}

	mm = slot->mm;
	down_read(&mm->mmap_sem);
	if (ksm_test_exit(mm))
		vma = NULL;
	else
		vma = find_vma(mm, ksm_scan.address);

	for (; vma; vma = vma->vm_next) {
		if (!(vma->vm_flags & VM_MERGEABLE))
			continue;
		if (ksm_scan.address < vma->vm_start)
			ksm_scan.address = vma->vm_start;
		if (!vma->anon_vma)
			ksm_scan.address = vma->vm_end;

		while (ksm_scan.address < vma->vm_end) {
			if (ksm_test_exit(mm))
				break;
			*page = follow_page(vma, ksm_scan.address, FOLL_GET);
			if (*page && PageAnon(*page)) {
				flush_anon_page(vma, *page, ksm_scan.address);
				flush_dcache_page(*page);
				rmap_item = get_next_rmap_item(slot,
					ksm_scan.rmap_item->link.next,
					ksm_scan.address);
				if (rmap_item) {
					ksm_scan.rmap_item = rmap_item;
					ksm_scan.address += PAGE_SIZE;
				} else
					put_page(*page);
				up_read(&mm->mmap_sem);
				return rmap_item;
			}
			if (*page)
				put_page(*page);
			ksm_scan.address += PAGE_SIZE;
			cond_resched();
		}
	}

	if (ksm_test_exit(mm)) {
		ksm_scan.address = 0;
		ksm_scan.rmap_item = list_entry(&slot->rmap_list,
						struct rmap_item, link);
	}
	/*
	 * Nuke all the rmap_items that are above this current rmap:
	 * because there were no VM_MERGEABLE vmas with such addresses.
	 */
	remove_trailing_rmap_items(slot, ksm_scan.rmap_item->link.next);

	spin_lock(&ksm_mmlist_lock);
	ksm_scan.mm_slot = list_entry(slot->mm_list.next,
						struct mm_slot, mm_list);
	if (ksm_scan.address == 0) {
		/*
		 * We've completed a full scan of all vmas, holding mmap_sem
		 * throughout, and found no VM_MERGEABLE: so do the same as
		 * __ksm_exit does to remove this mm from all our lists now.
		 * This applies either when cleaning up after __ksm_exit
		 * (but beware: we can reach here even before __ksm_exit),
		 * or when all VM_MERGEABLE areas have been unmapped (and
		 * mmap_sem then protects against race with MADV_MERGEABLE).
		 */
		hlist_del(&slot->link);
		list_del(&slot->mm_list);
		spin_unlock(&ksm_mmlist_lock);

		free_mm_slot(slot);
		clear_bit(MMF_VM_MERGEABLE, &mm->flags);
		up_read(&mm->mmap_sem);
		mmdrop(mm);
	} else {
		spin_unlock(&ksm_mmlist_lock);
		up_read(&mm->mmap_sem);
	}

	/* Repeat until we've completed scanning the whole list */
	slot = ksm_scan.mm_slot;
	if (slot != &ksm_mm_head)
		goto next_mm;

	ksm_scan.seqnr++;
	return NULL;
}

/**
 * ksm_do_scan  - the ksm scanner main worker function.
 * @scan_npages - number of pages we want to scan before we return.
 */
static void ksm_do_scan(unsigned int scan_npages)
{
	struct rmap_item *rmap_item;
	struct page *page;

	while (scan_npages--) {
		cond_resched();
		rmap_item = scan_get_next_rmap_item(&page);
		if (!rmap_item)
			return;
		if (!PageKsm(page) || !in_stable_tree(rmap_item))
			cmp_and_merge_page(page, rmap_item);
		else if (page_mapcount(page) == 1) {
			/*
			 * Replace now-unshared ksm page by ordinary page.
			 */
			break_cow(rmap_item->mm, rmap_item->address);
			remove_rmap_item_from_tree(rmap_item);
			rmap_item->oldchecksum = calc_checksum(page);
		}
		put_page(page);
	}
}

static int ksmd_should_run(void)
{
	return (ksm_run & KSM_RUN_MERGE) && !list_empty(&ksm_mm_head.mm_list);
}

static int ksm_scan_thread(void *nothing)
{
	set_user_nice(current, 5);

	while (!kthread_should_stop()) {
		mutex_lock(&ksm_thread_mutex);
		if (ksmd_should_run())
			ksm_do_scan(ksm_thread_pages_to_scan);
		mutex_unlock(&ksm_thread_mutex);

		if (ksmd_should_run()) {
			schedule_timeout_interruptible(
				msecs_to_jiffies(ksm_thread_sleep_millisecs));
		} else {
			wait_event_interruptible(ksm_thread_wait,
				ksmd_should_run() || kthread_should_stop());
		}
	}
	return 0;
}

int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	int err;

	switch (advice) {
	case MADV_MERGEABLE:
		/*
		 * Be somewhat over-protective for now!
		 */
		if (*vm_flags & (VM_MERGEABLE | VM_SHARED  | VM_MAYSHARE   |
				 VM_PFNMAP    | VM_IO      | VM_DONTEXPAND |
				 VM_RESERVED  | VM_HUGETLB | VM_INSERTPAGE |
				 VM_MIXEDMAP  | VM_SAO))
			return 0;		/* just ignore the advice */

		if (!test_bit(MMF_VM_MERGEABLE, &mm->flags)) {
			err = __ksm_enter(mm);
			if (err)
				return err;
		}

		*vm_flags |= VM_MERGEABLE;
		break;

	case MADV_UNMERGEABLE:
		if (!(*vm_flags & VM_MERGEABLE))
			return 0;		/* just ignore the advice */

		if (vma->anon_vma) {
			err = unmerge_ksm_pages(vma, start, end);
			if (err)
				return err;
		}

		*vm_flags &= ~VM_MERGEABLE;
		break;
	}

	return 0;
}

int __ksm_enter(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	int needs_wakeup;

	mm_slot = alloc_mm_slot();
	if (!mm_slot)
		return -ENOMEM;

	/* Check ksm_run too?  Would need tighter locking */
	needs_wakeup = list_empty(&ksm_mm_head.mm_list);

	spin_lock(&ksm_mmlist_lock);
	insert_to_mm_slots_hash(mm, mm_slot);
	/*
	 * Insert just behind the scanning cursor, to let the area settle
	 * down a little; when fork is followed by immediate exec, we don't
	 * want ksmd to waste time setting up and tearing down an rmap_list.
	 */
	list_add_tail(&mm_slot->mm_list, &ksm_scan.mm_slot->mm_list);
	spin_unlock(&ksm_mmlist_lock);

	set_bit(MMF_VM_MERGEABLE, &mm->flags);
	atomic_inc(&mm->mm_count);

	if (needs_wakeup)
		wake_up_interruptible(&ksm_thread_wait);

	return 0;
}

void __ksm_exit(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	int easy_to_free = 0;

	/*
	 * This process is exiting: if it's straightforward (as is the
	 * case when ksmd was never running), free mm_slot immediately.
	 * But if it's at the cursor or has rmap_items linked to it, use
	 * mmap_sem to synchronize with any break_cows before pagetables
	 * are freed, and leave the mm_slot on the list for ksmd to free.
	 * Beware: ksm may already have noticed it exiting and freed the slot.
	 */

	spin_lock(&ksm_mmlist_lock);
	mm_slot = get_mm_slot(mm);
	if (mm_slot && ksm_scan.mm_slot != mm_slot) {
		if (list_empty(&mm_slot->rmap_list)) {
			hlist_del(&mm_slot->link);
			list_del(&mm_slot->mm_list);
			easy_to_free = 1;
		} else {
			list_move(&mm_slot->mm_list,
				  &ksm_scan.mm_slot->mm_list);
		}
	}
	spin_unlock(&ksm_mmlist_lock);

	if (easy_to_free) {
		free_mm_slot(mm_slot);
		clear_bit(MMF_VM_MERGEABLE, &mm->flags);
		mmdrop(mm);
	} else if (mm_slot) {
		down_write(&mm->mmap_sem);
		up_write(&mm->mmap_sem);
	}
}

#ifdef CONFIG_SYSFS
/*
 * This all compiles without CONFIG_SYSFS, but is a waste of space.
 */

#define KSM_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define KSM_ATTR(_name) \
	static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

static ssize_t sleep_millisecs_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", ksm_thread_sleep_millisecs);
}

static ssize_t sleep_millisecs_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long msecs;
	int err;

	err = strict_strtoul(buf, 10, &msecs);
	if (err || msecs > UINT_MAX)
		return -EINVAL;

	ksm_thread_sleep_millisecs = msecs;

	return count;
}
KSM_ATTR(sleep_millisecs);

static ssize_t pages_to_scan_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", ksm_thread_pages_to_scan);
}

static ssize_t pages_to_scan_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int err;
	unsigned long nr_pages;

	err = strict_strtoul(buf, 10, &nr_pages);
	if (err || nr_pages > UINT_MAX)
		return -EINVAL;

	ksm_thread_pages_to_scan = nr_pages;

	return count;
}
KSM_ATTR(pages_to_scan);

static ssize_t run_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%u\n", ksm_run);
}

static ssize_t run_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	int err;
	unsigned long flags;

	err = strict_strtoul(buf, 10, &flags);
	if (err || flags > UINT_MAX)
		return -EINVAL;
	if (flags > KSM_RUN_UNMERGE)
		return -EINVAL;

	/*
	 * KSM_RUN_MERGE sets ksmd running, and 0 stops it running.
	 * KSM_RUN_UNMERGE stops it running and unmerges all rmap_items,
	 * breaking COW to free the unswappable pages_shared (but leaves
	 * mm_slots on the list for when ksmd may be set running again).
	 */

	mutex_lock(&ksm_thread_mutex);
	if (ksm_run != flags) {
		ksm_run = flags;
		if (flags & KSM_RUN_UNMERGE) {
			current->flags |= PF_OOM_ORIGIN;
			err = unmerge_and_remove_all_rmap_items();
			current->flags &= ~PF_OOM_ORIGIN;
			if (err) {
				ksm_run = KSM_RUN_STOP;
				count = err;
			}
		}
	}
	mutex_unlock(&ksm_thread_mutex);

	if (flags & KSM_RUN_MERGE)
		wake_up_interruptible(&ksm_thread_wait);

	return count;
}
KSM_ATTR(run);

static ssize_t max_kernel_pages_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int err;
	unsigned long nr_pages;

	err = strict_strtoul(buf, 10, &nr_pages);
	if (err)
		return -EINVAL;

	ksm_max_kernel_pages = nr_pages;

	return count;
}

static ssize_t max_kernel_pages_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_max_kernel_pages);
}
KSM_ATTR(max_kernel_pages);

static ssize_t pages_shared_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_pages_shared);
}
KSM_ATTR_RO(pages_shared);

static ssize_t pages_sharing_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_pages_sharing);
}
KSM_ATTR_RO(pages_sharing);

static ssize_t pages_unshared_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_pages_unshared);
}
KSM_ATTR_RO(pages_unshared);

static ssize_t pages_volatile_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	long ksm_pages_volatile;

	ksm_pages_volatile = ksm_rmap_items - ksm_pages_shared
				- ksm_pages_sharing - ksm_pages_unshared;
	/*
	 * It was not worth any locking to calculate that statistic,
	 * but it might therefore sometimes be negative: conceal that.
	 */
	if (ksm_pages_volatile < 0)
		ksm_pages_volatile = 0;
	return sprintf(buf, "%ld\n", ksm_pages_volatile);
}
KSM_ATTR_RO(pages_volatile);

static ssize_t full_scans_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_scan.seqnr);
}
KSM_ATTR_RO(full_scans);

static struct attribute *ksm_attrs[] = {
	&sleep_millisecs_attr.attr,
	&pages_to_scan_attr.attr,
	&run_attr.attr,
	&max_kernel_pages_attr.attr,
	&pages_shared_attr.attr,
	&pages_sharing_attr.attr,
	&pages_unshared_attr.attr,
	&pages_volatile_attr.attr,
	&full_scans_attr.attr,
	NULL,
};

static struct attribute_group ksm_attr_group = {
	.attrs = ksm_attrs,
	.name = "ksm",
};
#endif /* CONFIG_SYSFS */

static int __init ksm_init(void)
{
	struct task_struct *ksm_thread;
	int err;

	ksm_max_kernel_pages = totalram_pages / 4;

	err = ksm_slab_init();
	if (err)
		goto out;

	err = mm_slots_hash_init();
	if (err)
		goto out_free1;

	ksm_thread = kthread_run(ksm_scan_thread, NULL, "ksmd");
	if (IS_ERR(ksm_thread)) {
		printk(KERN_ERR "ksm: creating kthread failed\n");
		err = PTR_ERR(ksm_thread);
		goto out_free2;
	}

#ifdef CONFIG_SYSFS
	err = sysfs_create_group(mm_kobj, &ksm_attr_group);
	if (err) {
		printk(KERN_ERR "ksm: register sysfs failed\n");
		kthread_stop(ksm_thread);
		goto out_free2;
	}
#else
	//ksm_run = KSM_RUN_MERGE;	/* no way for user to start it */

#endif /* CONFIG_SYSFS */

	return 0;

out_free2:
	mm_slots_hash_free();
out_free1:
	ksm_slab_free();
out:
	return err;
}
module_init(ksm_init)
