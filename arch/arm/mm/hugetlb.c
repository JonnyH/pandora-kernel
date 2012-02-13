/*
 * hugetlb.c, ARM Huge Tlb Page support.
 *
 * Copyright (c) Bill Carson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/hugetlb.h>

pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr,
		      unsigned long sz)
{
	pte_t *linuxpte = mm->context.huge_linux_pte;
	int index;

	if (linuxpte == NULL) {
		linuxpte = kzalloc(HUGE_LINUX_PTE_SIZE, GFP_ATOMIC);
		if (linuxpte == NULL) {
			printk(KERN_ERR "Cannot allocate memory for huge linux pte\n");
			return NULL;
		}
		mm->context.huge_linux_pte = linuxpte;
	}
	/* huge page mapping only cover user space address */
	BUG_ON(HUGE_LINUX_PTE_INDEX(addr) >= HUGE_LINUX_PTE_COUNT);
	index = HUGE_LINUX_PTE_INDEX(addr);
	return &linuxpte[HUGE_LINUX_PTE_INDEX(addr)];
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd = NULL;
	pte_t *linuxpte = mm->context.huge_linux_pte;

	/* check this mapping exist at pmd level */
	pgd = pgd_offset(mm, addr);
	if (pgd_present(*pgd)) {
		pud = pud_offset(pgd, addr);
		pmd = pmd_offset(pud, addr);
		if (!pmd_present(*pmd))
			return NULL;
	}

	BUG_ON(HUGE_LINUX_PTE_INDEX(addr) >= HUGE_LINUX_PTE_COUNT);
	BUG_ON((*pmd & PMD_TYPE_MASK) != PMD_TYPE_SECT);
	return &linuxpte[HUGE_LINUX_PTE_INDEX(addr)];
}

int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	return 0;
}

struct page *follow_huge_addr(struct mm_struct *mm, unsigned long address,
				int write)
{
	return ERR_PTR(-EINVAL);
}

int pmd_huge(pmd_t pmd)
{
	return (pmd_val(pmd) & PMD_TYPE_MASK) == PMD_TYPE_SECT;
}

int pud_huge(pud_t pud)
{
	return  0; } struct page * follow_huge_pmd(struct mm_struct *mm, unsigned long address, pmd_t *pmd, int write)
{
	struct page *page = NULL;
	unsigned long pfn;

	BUG_ON((pmd_val(*pmd) & PMD_TYPE_MASK) != PMD_TYPE_SECT);
	pfn = ((pmd_val(*pmd) & HPAGE_MASK) >> PAGE_SHIFT);
	page = pfn_to_page(pfn);
	return page;
}

static int __init add_huge_page_size(unsigned long long size)
{
	int shift = __ffs(size);
	u32 mmfr3 = 0;

	/* Check that it is a page size supported by the hardware and
	 * that it fits within pagetable and slice limits. */
	if (!is_power_of_2(size) || (shift != HPAGE_SHIFT))
		return -EINVAL;

	/* If user wants super-section support, then check if our cpu
	 * has this feature supported in ID_MMFR3 */
	if (shift == SUPERSECTION_SHIFT) {
		__asm__("mrc p15, 0, %0, c0, c1, 7\n" : "=r" (mmfr3));
		if (mmfr3 & 0xF0000000) {
			printk("Super-Section is NOT supported by this CPU, mmfr3:0x%x\n", mmfr3);
			return -EINVAL;
		}
	}

	/* Return if huge page size has already been setup */
	if (size_to_hstate(size))
		return 0;

	hugetlb_add_hstate(shift - PAGE_SHIFT);
	return 0;
}

static int __init hugepage_setup_sz(char *str)
{
	unsigned long long size;

	size = memparse(str, &str);
	if (add_huge_page_size(size) != 0)
		printk(KERN_WARNING "Invalid huge page size specified(%llu)\n",
			 size);

	return 1;
}
__setup("hugepagesz=", hugepage_setup_sz);
