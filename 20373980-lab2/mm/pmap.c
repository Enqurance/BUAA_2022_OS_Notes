#include "mmu.h"
#include "pmap.h"
#include "printf.h"
#include "env.h"
#include "error.h"


/* These variables are set by mips_detect_memory() */
u_long maxpa;            /* Maximum physical address */
u_long npage;            /* Amount of memory(in pages) */
u_long basemem;          /* Amount of base memory(in bytes) */
u_long extmem;           /* Amount of extended memory(in bytes) */

Pde *boot_pgdir;

struct Page *pages;
static u_long freemem;

static struct Page_list page_free_list;	/* Free list of physical pages */


/* Exercise 2.1 */
/* Overview:
   Initialize basemem and npage.
   Set basemem to be 64MB, and calculate corresponding npage value.*/
void mips_detect_memory()
{
	/* Step 1: Initialize basemem.
	 * (When use real computer, CMOS tells us how many kilobytes there are). */
	maxpa = 64 * 1024 * 1024;		//maxpa，物理地址的最大值+1 
	basemem = 64 * 1024 * 1024;		//basemem，物理内存对应的字节数 
	extmem = 0;						//扩展内存，在本实验中为0 
	// Step 2: Calculate corresponding npage value.
	npage = maxpa / 4096;			//总物理页数 

	printf("Physical memory: %dK available, ", (int)(maxpa / 1024));
	printf("base = %dK, extended = %dK\n", (int)(basemem / 1024),
			(int)(extmem / 1024));
}

/* Overview:
   Allocate `n` bytes physical memory with alignment `align`, if `clear` is set, clear the
   allocated memory.
   This allocator is used only while setting up virtual memory system.

   Post-Condition:
   If we're out of memory, should panic, else return this address of memory we have allocated.*/
//本函数作用为分配n字节的空间并返回初始虚拟地址 
//n为所申请空间内存字节数，align为对齐大小，clear为清空位 
static void *alloc(u_int n, u_int align, int clear)
{
	extern char end[];
	u_long alloced_mem;

	/* Initialize `freemem` if this is the first time. The first virtual address that the
	 * linker did *not* assign to any kernel code or global variables. */
	/* 初始化freemem如果freemem没有被初始化过。end在其他文件有定义，值为0x80400000， 
	0x80400000是kseg0段的地址，这一段地址不需要通过MMU进行映射，直接去掉高三位即可
	作为物理地址使用。当然，kseg0这段地址对于MOS的物理内存来说也是比较大的，所以并
	不是其中所有的地址都会被使用，其范围应当是[0x8040000,0x82000000)，去掉高三位后
	对应的物理内存就是[0x000000,0x2000000) */ 
	if (freemem == 0) {
		freemem = (u_long)end;
	}

	/* Step 1: Round up `freemem` up to be aligned properly */
	//将freemem按照align进行对齐 ，此时的freemem为初始虚拟地址 
	freemem = ROUND(freemem, align);

	/* Step 2: Save current value of `freemem` as allocated chunk. */
	//将alloced_mem赋值为freemem，即初始虚拟地址 
	alloced_mem = freemem;

	/* Step 3: Increase `freemem` to record allocation. */
	//freemem增加n字节，表示[0x8040000,freemem-1]的空间被使用了 
	freemem = freemem + n;

	/* Check if we're out of memory. If we are, PANIC !! */
	/* 检查是否发生内存越界。用PADDR()将freemem转化成物理内存，并和最大物理内存作
	比较。上面提过，物理内存的值是0x2000000，即64MB*/ 
	if (PADDR(freemem) >= maxpa) {
		panic("out of memory\n");
		return (void *)-E_NO_MEM;
	}

	/* Step 4: Clear allocated chunk if parameter `clear` is set. */
	//如果clear位有效，则将空间清零 
	if (clear) {
		bzero((void *)alloced_mem, n);
	}

	/* Step 5: return allocated chunk. */
	//返回开辟的虚拟内存初始地址 
	return (void *)alloced_mem;
}

/* Exercise 2.6 */
/* Overview:
   Get the page table entry for virtual address `va` in the given
   page directory `pgdir`.
   If the page table is not exist and the parameter `create` is set to 1,
   then create it.*/
/*为虚拟地址va获取其对应的pgdir的页表项。 如果页表项不存在，且create置为1，
则创建这个页表项。这是在一级页表中载入二级页表的过程 。这个函数实际上是在
一级页表的内存中填充有关va虚拟地址对应的二级页表的信息，而不是在填写二级
页表的内容 
*/ 
static Pte *boot_pgdir_walk(Pde *pgdir, u_long va, int create)
{
	//Pde，一级页表项；Pte，二级页表项 
	Pde *pgdir_entryp;
	Pte *pgtable, *pgtable_entry;

	/* Step 1: Get the corresponding page directory entry and page table. */
	/* Hint: Use KADDR and PTE_ADDR to get the page table from page directory
	 * entry value. */
	//PDX(va)可以获得虚拟地址的31-22位，pgdir_entryp可以得到其对应的页表项地址 
	pgdir_entryp = pgdir + PDX(va);
	/* Step 2: If the corresponding page table is not exist and parameter `create`
	 * is set, create one. And set the correct permission bits for this new page
	 * table. */
	//页表项不存在，且允许创建，则创建页表项 
	if ((*pgdir_entryp & PTE_V) == 0) {
        if (create) {
        	//在freemem基础上开辟空间并将虚拟地址转换，令一级页表项的内容为此地址 
    		*pgdir_entryp = PADDR(alloc(BY2PG, BY2PG, 1));
    		//给一级页表项的地址的权限位赋予权限，PTE_V标记是否有效，PTE_R标记是否可写 
			*pgdir_entryp = (*pgdir_entryp) | PTE_V | PTE_R;
            /**
            * use `alloc` to allocate a page for the page table
            * set permission: `PTE_V | PTE_R`
            * hint: `PTE_V` <==> valid ; `PTE_R` <==> writable
            */
        } else return 0; // exception
    }
	/* Step 3: Get the page table entry for `va`, and return it. */
	//获取va地址的页表条目，并返回。先将当前地址转化为耳机页表项地址，再组合成虚拟地址返回 
	return ((Pte *)(KADDR(PTE_ADDR(*pgdir_entryp)))) + PTX(va);
}

/* Exercise 2.7 */
/*Overview:
  Map [va, va+size) of virtual address space to physical [pa, pa+size) in the page
  table rooted at pgdir.
  Use permission bits `perm | PTE_V` for the entries.

  Pre-Condition:
  Size is a multiple of BY2PG.*/
/*将[va,va+size)的虚拟地址段映射到[pa,pa+size)的物理地址段上，且赋予其权限
前提条件：size是BY2PG的倍数 。这个函数真正地在填写二级页表的物理空间 
*/ 
void boot_map_segment(Pde *pgdir, u_long va, u_long size, u_long pa, int perm)
{
	int i, va_temp;
	//Pte指二级页表项 
	Pte *pgtable_entry;

	/* Step 1: Check if `size` is a multiple of BY2PG. */
	for (i = 0, size = ROUND(size, BY2PG); i < size; i += BY2PG) {
        /* Step 1. use `boot_pgdir_walk` to "walk" the page directory */
        //为一级页表的第i个页表项指定内存空间，即给二级页表项指定内存空间 
        pgtable_entry = boot_pgdir_walk(pgdir, va + i, 1);
        /* Step 2. fill in the page table */
        //在这个二级页表的内存空间中写入相关信息 
        *pgtable_entry = (pa + i) | perm | PTE_V;
    }

	/* Step 2: Map virtual address space to physical address. */
	/* Hint: Use `boot_pgdir_walk` to get the page table entry of virtual address `va`. */


}

/* Overview:
   Set up two-level page table.

Hint:
You can get more details about `UPAGES` and `UENVS` in include/mmu.h. */
void mips_vm_init()
{
	extern char end[];
	extern int mCONTEXT;
	extern struct Env *envs;

	Pde *pgdir;
	u_int n;

	/* Step 1: Allocate a page for page directory(first level page table). */
	//首先，为页表目录pgdir声明一个内存空间，大小对、齐方式为BY2PG，即4KB 
	pgdir = alloc(BY2PG, BY2PG, 1);
	printf("to memory %x for struct page directory.\n", freemem);
	mCONTEXT = (int)pgdir;

	boot_pgdir = pgdir;

	/* Step 2: Allocate proper size of physical memory for global array `pages`,
	 * for physical memory management. Then, map virtual address `UPAGES` to
	 * physical address `pages` allocated before. In consideration of alignment,
	 * you should round up the memory size before map. */
	/* 
	UPAGES映射到之前声明的物理地址pages上。为了保证页对齐，需要在映射前进行对齐 
	*/
	//需要npage个Page结构体大小的空间，且对齐方式为BY2PG。此时freemem已经移动了
	//声明后，pages是存放众多Page结构体的内存段的初始地址 
	pages = (struct Page *)alloc(npage * sizeof(struct Page), BY2PG, 1);
	printf("to memory %x for struct Pages.\n", freemem);
	//进行对齐操作 
	n = ROUND(npage * sizeof(struct Page), BY2PG);
	boot_map_segment(pgdir, UPAGES, n, PADDR(pages), PTE_R);

	/* Step 3, Allocate proper size of physical memory for global array `envs`,
	 * for process management. Then map the physical address to `UENVS`. */
	/* 为全局数组envs声明正确的物理内存空间，用于进程管理。然后将物理地址映射到UENVS
	采用PADDR与KADDR这两个宏就可以对位于kseg0/的虚拟地址和对应的物理地址进行转换 
	*/ 
	envs = (struct Env *)alloc(NENV * sizeof(struct Env), BY2PG, 1);
	n = ROUND(NENV * sizeof(struct Env), BY2PG);
	boot_map_segment(pgdir, UENVS, n, PADDR(envs), PTE_R);

	printf("pmap.c:\t mips vm init success\n");
}

/* Exercise 2.3 */
/*Overview:
  Initialize page structure and memory free list.
  The `pages` array has one `struct Page` entry per physical page. Pages
  are reference counted, and free pages are kept on a linked list.
Hint:
Use `LIST_INSERT_HEAD` to insert something to list.*/
void page_init(void)
{
	/* Step 1: Initialize page_free_list. */
	/* Hint: Use macro `LIST_INIT` defined in include/queue.h. */
	/*
		#define LIST_INIT(head) do {                                            \
            LIST_FIRST((head)) = NULL;                              \
        } while (0)
	*/
	LIST_INIT(&page_free_list);
	/* Step 2: Align `freemem` up to multiple of BY2PG. */
	ROUND(freemem, BY2PG);
	/* Step 3: Mark all memory blow `freemem` as used(set `pp_ref`
	 * filed to 1) */
	struct Page *cur = pages;
	for(;page2kva(cur) < freemem;cur++)
	{
		cur->pp_ref = 1;	
	}

	/* Step 4: Mark the other memory as free. */
	/* PNN(va)，可将虚拟地址右移12位即除以4KB。此处，首先将freemem转换为
	物理地址，然后使用PPN(va)宏得到该物理地址对应的物理页号，用它作为下标，
	从page数组中取地址。显然，此地址是page数组的最大下标的上限——这是因为
	我们需要将剩下的物理页标记为空闲，所以需要从这一地址开始。然后，我们
	设立循环结束条件page2ppn(cur)<npage。page2ppn(struct Page *p)，将p传入
	可以得到其对应的物理页号。这个循环的意思就是，将pages数组上、npage下的
	物理页全部标记为使用过。首先将pp_ref置为0，然后将该物理页插入链表头部 */
	for (cur = &pages[PPN(PADDR(freemem))];page2ppn(cur) < npage;cur++)
	{
		cur->pp_ref = 0;
		LIST_INSERT_HEAD(&page_free_list, cur, pp_link);
	}
}

/* Exercise 2.4 */
/*Overview:
  Allocates a physical page from free memory, and clear this page.

  Post-Condition:
  If failing to allocate a new page(out of memory(there's no free page)),
  return -E_NO_MEM.
  Else, set the address of allocated page to *pp, and return 0.

Note:
DO NOT increment the reference count of the page - the caller must do
these if necessary (either explicitly or via page_insert).

Hint:
Use LIST_FIRST and LIST_REMOVE defined in include/queue.h .*/
/*此函数的作用是在空闲内存中声明一个物理页，并且将这一页清空。倘若声明失败， 
即内存越界，则返回-E_NO_MEM；倘若声明成功，则令*pp为所声明地址的初始空间，
并且返回0 */ 
int page_alloc(struct Page **pp)
{
	struct Page *ppage_temp;

	/* Step 1: Get a page from free memory. If fail, return the error code.*/
	/* 首先判断内存管理结构体是否已经创建好（即判断&pae_free_list是否为空），
	当然，由于执行过init系列函数，一般都是已经开辟好的 */
	if (LIST_EMPTY(&page_free_list)) return -E_NO_MEM;
	/* Step 2: Initialize this page.
	 * Hint: use `bzero`. */
	 /* 然后初始化页面。用Page *类型变量指向结构体链表的第一个节点，可以通过
	 LIST_FIRST宏完成。然后，使用LIST_REMOVE宏将头节点从链表中删除，即将其从
	 空闲链表结构体中移出。再之后，使用bzero()函数将其清空，并将ppage_temp的
	 值赋给*pp */
	ppage_temp = LIST_FIRST(&page_free_list);
	LIST_REMOVE(ppage_temp, pp_link);
	bzero(page2kva(ppage_temp), BY2PG);
	*pp = ppage_temp;
	return 0;
}

/* Exercise 2.5 */
/*Overview:
  Release a page, mark it as free if it's `pp_ref` reaches 0.
Hint:
When you free a page, just insert it to the page_free_list.*/
void page_free(struct Page *pp)
{
	/* Step 1: If there's still virtual address referring to this page, do nothing. */
	/* Step 2: If the `pp_ref` reaches 0, mark this page as free and return. */
 	if (pp->pp_ref == 0) {
		LIST_INSERT_HEAD(&page_free_list, pp, pp_link);
    	return;
  	} else if (pp->pp_ref > 0) return; // in use
	/* If the value of `pp_ref` is less than 0, some error must occurr before,
	 * so PANIC !!! */
	panic("cgh:pp->pp_ref is less than zero\n");
}

/* Exercise 2.8 */
/*Overview:
  Given `pgdir`, a pointer to a page directory, pgdir_walk returns a pointer
  to the page table entry (with permission PTE_R|PTE_V) for virtual address 'va'.

  Pre-Condition:
  The `pgdir` should be two-level page table structure.

  Post-Condition:
  If we're out of memory, return -E_NO_MEM.
  Else, we get the page table entry successfully, store the value of page table
  entry to *ppte, and return 0, indicating success.

Hint:
We use a two-level pointer to store page table entry and return a state code to indicate
whether this function execute successfully or not.
This function has something in common with function `boot_pgdir_walk`.*/
int pgdir_walk(Pde *pgdir, u_long va, int create, Pte **ppte)
{
	/* Step 1: Get the corresponding page directory entry and page table. */
	/* Step 2: If the corresponding page table is not exist(valid) and parameter `create`
	 * is set, create one. And set the correct permission bits for this new page table.
	 * When creating new page table, maybe out of memory. */
	/* Step 3: Set the page table entry to `*ppte` as return value. */
	Pde *pgdir_entry = pgdir + PDX(va);
    struct Page *page;
    int ret;
    
    // check whether the page table exists
    if ((*pgdir_entry & PTE_V) == 0) {
        if (create) {
            if ((ret = page_alloc(&page)) < 0) return ret;
            *pgdir_entry = (page2pa(page))
                          | PTE_V | PTE_R;
        	page->pp_ref++;
		} else {
            *ppte = 0;
            return 0;
        }
    }
    *ppte = ((Pte *)(KADDR(PTE_ADDR(*pgdir_entry)))) + PTX(va);
	return 0;
}

/* Exercise 2.9 */
/*Overview:
  Map the physical page 'pp' at virtual address 'va'.
  The permissions (the low 12 bits) of the page table entry should be set to 'perm|PTE_V'.

  Post-Condition:
  Return 0 on success
  Return -E_NO_MEM, if page table couldn't be allocated

Hint:
If there is already a page mapped at `va`, call page_remove() to release this mapping.
The `pp_ref` should be incremented if the insertion succeeds.*/
int page_insert(Pde *pgdir, struct Page *pp, u_long va, u_int perm)
{
	u_int PERM;
	Pte *pgtable_entry;
	PERM = perm | PTE_V;

	/* Step 1: Get corresponding page table entry. */
	pgdir_walk(pgdir, va, 0, &pgtable_entry);

	if (pgtable_entry != 0 && (*pgtable_entry & PTE_V) != 0) {
		if (pa2page(*pgtable_entry) != pp) {
			page_remove(pgdir, va);
		} else	{
			tlb_invalidate(pgdir, va);
			*pgtable_entry = (page2pa(pp) | PERM);
			return 0;
		}
	}

	/* Step 2: Update TLB. */
	/* hint: use tlb_invalidate function */
	tlb_invalidate(pgdir, va);
	/* Step 3: Do check, re-get page table entry to validate the insertion. */
	/* Step 3.1 Check if the page can be insert, if can鈥檛 return -E_NO_MEM */
	/* Step 3.2 Insert page and increment the pp_ref */
	if(pgdir_walk(pgdir, va, 1, &pgtable_entry) != 0)
	{
		return -E_NO_MEM;
	}
	*pgtable_entry = page2pa(pp) | PERM;
	pp->pp_ref++;
	return 0;
}

/*Overview:
  Look up the Page that virtual address `va` map to.

  Post-Condition:
  Return a pointer to corresponding Page, and store it's page table entry to *ppte.
  If `va` doesn't mapped to any Page, return NULL.*/
struct Page *page_lookup(Pde *pgdir, u_long va, Pte **ppte)
{
	struct Page *ppage;
	Pte *pte;

	/* Step 1: Get the page table entry. */
	pgdir_walk(pgdir, va, 0, &pte);

	/* Hint: Check if the page table entry doesn't exist or is not valid. */
	if (pte == 0) {
		return 0;
	}
	if ((*pte & PTE_V) == 0) {
		return 0;    //the page is not in memory.
	}

	/* Step 2: Get the corresponding Page struct. */

	/* Hint: Use function `pa2page`, defined in include/pmap.h . */
	ppage = pa2page(*pte);
	if (ppte) {
		*ppte = pte;
	}

	return ppage;
}

// Overview:
// 	Decrease the `pp_ref` value of Page `*pp`, if `pp_ref` reaches to 0, free this page.
void page_decref(struct Page *pp) {
	if(--pp->pp_ref == 0) {
		page_free(pp);
	}
}

// Overview:
// 	Unmaps the physical page at virtual address `va`.
void page_remove(Pde *pgdir, u_long va)
{
	Pte *pagetable_entry;
	struct Page *ppage;

	/* Step 1: Get the page table entry, and check if the page table entry is valid. */

	ppage = page_lookup(pgdir, va, &pagetable_entry);

	if (ppage == 0) {
		return;
	}

	/* Step 2: Decrease `pp_ref` and decide if it's necessary to free this page. */

	/* Hint: When there's no virtual address mapped to this page, release it. */
	ppage->pp_ref--;
	if (ppage->pp_ref == 0) {
		page_free(ppage);
	}

	/* Step 3: Update TLB. */
	*pagetable_entry = 0;
	tlb_invalidate(pgdir, va);
	return;
}

// Overview:
// 	Update TLB.
void tlb_invalidate(Pde *pgdir, u_long va)
{
	if (curenv) {
		tlb_out(PTE_ADDR(va) | GET_ENV_ASID(curenv->env_id));
	} else {
		tlb_out(PTE_ADDR(va));
	}
}

void physical_memory_manage_check(void)
{
	struct Page *pp, *pp0, *pp1, *pp2;
	struct Page_list fl;
	int *temp;

	// should be able to allocate three pages
	pp0 = pp1 = pp2 = 0;
	assert(page_alloc(&pp0) == 0);
	assert(page_alloc(&pp1) == 0);
	assert(page_alloc(&pp2) == 0);

	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);



	// temporarily steal the rest of the free pages
	fl = page_free_list;
	// now this page_free list must be empty!!!!
	LIST_INIT(&page_free_list);
	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	temp = (int*)page2kva(pp0);
	//write 1000 to pp0
	*temp = 1000;
	// free pp0
	page_free(pp0);
	printf("The number in address temp is %d\n",*temp);

	// alloc again
	assert(page_alloc(&pp0) == 0);
	assert(pp0);

	// pp0 should not change
	assert(temp == (int*)page2kva(pp0));
	// pp0 should be zero
	assert(*temp == 0);

	page_free_list = fl;
	page_free(pp0);
	page_free(pp1);
	page_free(pp2);
	struct Page_list test_free;
	struct Page *test_pages;
	test_pages= (struct Page *)alloc(10 * sizeof(struct Page), BY2PG, 1);
	LIST_INIT(&test_free);
	//LIST_FIRST(&test_free) = &test_pages[0];
	int i,j=0;
	struct Page *p, *q;
	//test inert tail
	for(i=0;i<10;i++) {
		test_pages[i].pp_ref=i;
		//test_pages[i].pp_link=NULL;
		//printf("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);
		LIST_INSERT_TAIL(&test_free,&test_pages[i],pp_link);
		//printf("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);

	}
	p = LIST_FIRST(&test_free);
	int answer1[]={0,1,2,3,4,5,6,7,8,9};
	assert(p!=NULL);
	while(p!=NULL)
	{
		//printf("%d %d\n",p->pp_ref,answer1[j]);
		assert(p->pp_ref==answer1[j++]);
		//printf("ptr: 0x%x v: %d\n",(p->pp_link).le_next,((p->pp_link).le_next)->pp_ref);
		p=LIST_NEXT(p,pp_link);

	}
	// insert_after test
	int answer2[]={0,1,2,3,4,20,5,6,7,8,9};
	q=(struct Page *)alloc(sizeof(struct Page), BY2PG, 1);
	q->pp_ref = 20;

	//printf("---%d\n",test_pages[4].pp_ref);
	LIST_INSERT_AFTER(&test_pages[4], q, pp_link);
	//printf("---%d\n",LIST_NEXT(&test_pages[4],pp_link)->pp_ref);
	p = LIST_FIRST(&test_free);
	j=0;
	//printf("into test\n");
	while(p!=NULL){
		//      printf("%d %d\n",p->pp_ref,answer2[j]);
		assert(p->pp_ref==answer2[j++]);
		p=LIST_NEXT(p,pp_link);
	}



	printf("physical_memory_manage_check() succeeded\n");
}


void page_check(void)
{
	struct Page *pp, *pp0, *pp1, *pp2;
	struct Page_list fl;

	// should be able to allocate three pages
	pp0 = pp1 = pp2 = 0;
	assert(page_alloc(&pp0) == 0);
	assert(page_alloc(&pp1) == 0);
	assert(page_alloc(&pp2) == 0);

	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);

	// temporarily steal the rest of the free pages
	fl = page_free_list;
	// now this page_free list must be empty!!!!
	LIST_INIT(&page_free_list);

	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	// there is no free memory, so we can't allocate a page table
	assert(page_insert(boot_pgdir, pp1, 0x0, 0) < 0);

	// free pp0 and try again: pp0 should be used for page table
	page_free(pp0);
	assert(page_insert(boot_pgdir, pp1, 0x0, 0) == 0);
	assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));

	printf("va2pa(boot_pgdir, 0x0) is %x\n",va2pa(boot_pgdir, 0x0));
	printf("page2pa(pp1) is %x\n",page2pa(pp1));

	assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
	assert(pp1->pp_ref == 1);

	// should be able to map pp2 at BY2PG because pp0 is already allocated for page table
	assert(page_insert(boot_pgdir, pp2, BY2PG, 0) == 0);
	assert(va2pa(boot_pgdir, BY2PG) == page2pa(pp2));
	assert(pp2->pp_ref == 1);

	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	printf("start page_insert\n");
	// should be able to map pp2 at BY2PG because it's already there
	assert(page_insert(boot_pgdir, pp2, BY2PG, 0) == 0);
	assert(va2pa(boot_pgdir, BY2PG) == page2pa(pp2));
	assert(pp2->pp_ref == 1);

	// pp2 should NOT be on the free list
	// could happen in ref counts are handled sloppily in page_insert
	assert(page_alloc(&pp) == -E_NO_MEM);

	// should not be able to map at PDMAP because need free page for page table
	assert(page_insert(boot_pgdir, pp0, PDMAP, 0) < 0);

	// insert pp1 at BY2PG (replacing pp2)
	assert(page_insert(boot_pgdir, pp1, BY2PG, 0) == 0);

	// should have pp1 at both 0 and BY2PG, pp2 nowhere, ...
	assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
	assert(va2pa(boot_pgdir, BY2PG) == page2pa(pp1));
	// ... and ref counts should reflect this
	assert(pp1->pp_ref == 2);
	printf("pp2->pp_ref %d\n",pp2->pp_ref);
	assert(pp2->pp_ref == 0);
	printf("end page_insert\n");

	// pp2 should be returned by page_alloc
	assert(page_alloc(&pp) == 0 && pp == pp2);

	// unmapping pp1 at 0 should keep pp1 at BY2PG
	page_remove(boot_pgdir, 0x0);
	assert(va2pa(boot_pgdir, 0x0) == ~0);
	assert(va2pa(boot_pgdir, BY2PG) == page2pa(pp1));
	assert(pp1->pp_ref == 1);
	assert(pp2->pp_ref == 0);

	// unmapping pp1 at BY2PG should free it
	page_remove(boot_pgdir, BY2PG);
	assert(va2pa(boot_pgdir, 0x0) == ~0);
	assert(va2pa(boot_pgdir, BY2PG) == ~0);
	assert(pp1->pp_ref == 0);
	assert(pp2->pp_ref == 0);

	// so it should be returned by page_alloc
	assert(page_alloc(&pp) == 0 && pp == pp1);

	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	// forcibly take pp0 back
	assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
	boot_pgdir[0] = 0;
	assert(pp0->pp_ref == 1);
	pp0->pp_ref = 0;

	// give free list back
	page_free_list = fl;

	// free the pages we took
	page_free(pp0);
	page_free(pp1);
	page_free(pp2);

	printf("page_check() succeeded!\n");
}

void pageout(int va, int context)
{
	u_long r;
	struct Page *p = NULL;

	if (context < 0x80000000) {
		panic("tlb refill and alloc error!");
	}

	if ((va > 0x7f400000) && (va < 0x7f800000)) {
		panic(">>>>>>>>>>>>>>>>>>>>>>it's env's zone");
	}

	if (va < 0x10000) {
		panic("^^^^^^TOO LOW^^^^^^^^^");
	}

	if ((r = page_alloc(&p)) < 0) {
		panic ("page alloc error!");
	}

	p->pp_ref++;

	page_insert((Pde *)context, p, VA2PFN(va), PTE_R);
	printf("pageout:\t@@@___0x%x___@@@  ins a page \n", va);
}

