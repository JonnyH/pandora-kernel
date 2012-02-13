/*
 * hugetlb.h, ARM Huge Tlb Page support.
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

#ifndef __ASM_HUGETLB_H
#define __ASM_HUGETLB_H

#include <asm/page.h>
#include <asm/pgtable-2level.h>
#include <asm/tlb.h>


/* 2M and 16M hugepage linux ptes are stored in mmu_context_t->huge_linux_pte
 *
 * 2M hugepage
 * ===========
 * one huge linux pte caters to two HW ptes,
 *
 * 16M hugepage
 * ============
 * one huge linux pte caters for sixteen HW ptes,
 *
 * The number of huge linux ptes depends on PAGE_OFFSET configuration
 * which is defined as following:
 */
#define HUGE_LINUX_PTE_COUNT	( PAGE_OFFSET >> HPAGE_SHIFT)
#define HUGE_LINUX_PTE_SIZE		(HUGE_LINUX_PTE_COUNT * sizeof(pte_t *))
#define HUGE_LINUX_PTE_INDEX(addr) (addr >> HPAGE_SHIFT)

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len)
{
	return 0;
}

static inline int prepare_hugepage_range(struct file *file,
					 unsigned long addr,
					 unsigned long len)
{
	struct hstate *h = hstate_file(file);
	/* addr/len should be aligned with huge page size */
	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (addr & ~huge_page_mask(h))
		return -EINVAL;

	return 0;
}

static inline void hugetlb_prefault_arch_hook(struct mm_struct *mm)
{
}

static inline void hugetlb_free_pgd_range(struct mmu_gather *tlb,
			unsigned long addr, unsigned long end,
			unsigned long floor, unsigned long ceiling)
{
}

static inline void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, pte_t pte)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *linuxpte = mm->context.huge_linux_pte;

	BUG_ON(linuxpte == NULL);
	BUG_ON(HUGE_LINUX_PTE_INDEX(addr) >= HUGE_LINUX_PTE_COUNT);
	BUG_ON(ptep != &linuxpte[HUGE_LINUX_PTE_INDEX(addr)]);

	/* set huge linux pte first */
	*ptep = pte;

	/* then set hardware pte */
	addr &= HPAGE_MASK;
	pgd = pgd_offset(mm, addr);
	pud = pud_offset(pgd, addr);
	pmd = pmd_offset(pud, addr);
	set_hugepte_at(mm, addr, pmd, pte);
}

static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;
	pte_t fake = L_PTE_YOUNG;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	/* clear linux pte */
	*ptep = 0;

	/* let set_hugepte_at clear HW entry */
	addr &= HPAGE_MASK;
	pgd = pgd_offset(mm, addr);
	pud = pud_offset(pgd, addr);
	pmd = pmd_offset(pud, addr);
	set_hugepte_at(mm, addr, pmd, fake);
	return pte;
}

static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep)
{
	if (HPAGE_SHIFT == SUPERSECTION_SHIFT)
		flush_tlb_page(vma, addr & SUPERSECTION_MASK);
	else {
		flush_tlb_page(vma, addr & SECTION_MASK);
		flush_tlb_page(vma, (addr & SECTION_MASK)^0x100000);
	}
}

static inline int huge_pte_none(pte_t pte)
{
	return pte_none(pte);
}

static inline pte_t huge_pte_wrprotect(pte_t pte)
{
	return pte_wrprotect(pte);
}

static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	pte_t old_pte = *ptep;
	set_huge_pte_at(mm, addr, ptep, pte_wrprotect(old_pte));
}

static inline pte_t huge_ptep_get(pte_t *ptep)
{
	return *ptep;
}

static inline int huge_ptep_set_access_flags(struct vm_area_struct *vma,
					     unsigned long addr,
					     pte_t *ptep, pte_t pte,
					     int dirty)
{
	int changed = !pte_same(huge_ptep_get(ptep), pte);
	if (changed) {
		set_huge_pte_at(vma->vm_mm, addr, ptep, pte);
		huge_ptep_clear_flush(vma, addr, &pte);
	}

	return changed;
}

static inline int arch_prepare_hugepage(struct page *page)
{
	return 0;
}

static inline void arch_release_hugepage(struct page *page)
{
}

#endif /* __ASM_HUGETLB_H */

