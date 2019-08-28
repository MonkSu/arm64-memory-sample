/*
页全局目录（Page Global Directory）
页上级目录（Page Upper Directory）
页中间目录（Page Middle Directory）
页表（Page Table）

五个类型转换宏（_ pte、_ pmd、_ pud、_ pgd和__ pgprot）把一个无符号整数转换成所需的类型。
五个类型转换宏（pte_val，pmd_val， pud_val， pgd_val和pgprot_val）执行相反的转换
这里需要区别指向页表项的指针和页表项所代表的数据。
以pgd_t类型为例子，如果已知一个pgd_t类型的指针pgd，那么通过pgd_val(*pgd)即可获得该页表项(也就是一个无符号长整型数据)


PTRS_PER_PGD    //页全局目录表个数
PTRS_PER_PUD    //页上级目录表个数
PTRS_PER_PMD    //页中间目录表个数
PTRS_PER_PTE    //页表个数

*/

#include <asm/io.h>
#include <asm/kernel-pgtable.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mman.h>
#include <linux/atomic.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/memblock.h>
#include <linux/mm.h>


#define MAP_SIZE                        (PAGE_SIZE*10)

#define ALLOC_VMALLOC                   0
#define ALLOC_ALLOC_PAGE                1
#define ALLOC_KMALLOC                   2
#define ALLOC_TYPE                      ALLOC_VMALLOC

#define MMAP_REMAP                      0
#define MMAP_FAULT                      1
#if (ALLOC_TYPE == ALLOC_VMALLOC)
#define MMAP_TYPE                       MMAP_FAULT
#else
#define MMAP_TYPE                       MMAP_REMAP
#endif

struct mapdrv{
    struct cdev mapdev;
    struct class *map_class;
    struct device *map_device;
    atomic_t usage;
};

extern struct mm_struct init_mm;
static struct mapdrv* md;
static char* vmalloc_area = NULL;
static void* kmalloc_vaddr = 0UL;
static void* page_vaddr = 0UL;

static int major;        /* major number of device */
static int minor;        /* minor number of device */

/* vmalloc addr to linear map addr
   1, Find the physical page corresponding to the vmalloc address
   2, Calculate the virtual memory address corresponding to the physical page address
 */

static volatile void *vaddr_to_kaddr(volatile void *address)
{
    pgd_t *pgd;
    pmd_t *pmd;
    pud_t *pud;
    pte_t *ptep, pte;
    unsigned long vma;
    void *phyaddr, *virtaddr;
    vma = (unsigned long)address;
    printk("--------------------------------------------------------------------------\n");
    printk(" %s %d Monk address:     0x%016lX\n",__func__,__LINE__, address);
    printk(" %s %d Monk init_mm.pgd: 0x%016lX\n",__func__,__LINE__,init_mm.pgd);
    /* get the page directory. */
    pgd = pgd_offset_k(vma);
    printk(" %s %d Monk pgd val:     0x%016lX\n",__func__,__LINE__, pgd_val(*pgd));
    printk(" %s %d Monk pgd index:   0x%016lX\n",__func__,__LINE__, pgd_index(vma));
    /* check whether we found an entry */
    if (!pgd_none(*pgd)) {
        pud = pud_offset(pgd, vma);
        // printk(" %s %d Monk pud: 0x%016lX\n",__func__,__LINE__, pud_val(*pud));
        // printk(" %s %d Monk pud: 0x%016lX\n",__func__,__LINE__, pud_index(vma));
        if (!pud_none(*pud)) {
            /* get the page middle directory */
            pmd = pmd_offset(pud, vma);
            printk(" %s %d Monk pmd val:     0x%016lX\n",__func__,__LINE__, pmd_val(*pmd));
            printk(" %s %d Monk pmd index:   0x%016lX\n",__func__,__LINE__, pmd_index(vma));
            /* check whether we found an entry */
            if (!pmd_none(*pmd)) {
                /* get a pointer to the page table entry */
                ptep = pte_offset_kernel(pmd, vma);
                pte = *ptep;
                printk(" %s %d Monk pte val:    0x%016lX\n",__func__,__LINE__, pte_val(pte));
                printk(" %s %d Monk pte index:  0x%016lX\n",__func__,__LINE__, pte_index(vma));
                /* check for a valid page */
                if (pte_present(pte)) {
                    /* get the address the page is refering to */
                    phyaddr  = (void*)page_to_phys(pte_page(pte));
                    virtaddr = (void*)phys_to_virt((unsigned long)phyaddr);
                    printk(" %s %d Monk page phy :  0x%016lX\n",__func__,__LINE__, phyaddr);
                    printk(" %s %d Monk page virt:  0x%016lX\n",__func__,__LINE__, virtaddr);
                }
            }
        }
    }
    printk("--------------------------------------------------------------------------\n");
    return ((volatile void *)virtaddr);
}


/* open handler for vm area */
static void map_vopen(struct vm_area_struct *vma)
{
    printk(" %s %d Monk \n",__func__,__LINE__);
    printk(" %s %d Monk vmalloc addr:            0x%016lX\n",__func__,__LINE__, vmalloc_area);
    printk(" %s %d Monk vmalloc data:            %s\n",__func__,__LINE__, vmalloc_area);
}

/* close handler form vm area */
static void map_vclose(struct vm_area_struct *vma)
{
    printk(" %s %d Monk \n",__func__,__LINE__);
    printk(" %s %d Monk vmalloc addr:            0x%016lX\n",__func__,__LINE__, vmalloc_area);
    printk(" %s %d Monk vmalloc data:            %s\n",__func__,__LINE__, vmalloc_area);
}
#if (MMAP_TYPE == MMAP_FAULT)
/* page fault handler */
static int map_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    unsigned long offset;
    unsigned long pg_offset;
    unsigned long virt_kaddr;
    unsigned long address = (unsigned long)vmf->virtual_address;
    pgoff_t pgoff = vmf->pgoff;

    printk(" %s %d Monk vmf->virtual_address:    0x%016lX\n",__func__,__LINE__, vmf->virtual_address);
    /* determine the offset within the vmalloc'd area */
    offset =  address - vma->vm_start + (vma->vm_pgoff << PAGE_SHIFT);
    pg_offset = (pgoff << PAGE_SHIFT) + (vma->vm_pgoff << PAGE_SHIFT);
    printk(" %s %d Monk vma->vm_start:           0x%016lX\n",__func__,__LINE__, vma->vm_start);
    printk(" %s %d Monk vma->vm_pgoff:           0x%016lX\n",__func__,__LINE__, vma->vm_pgoff);
    printk(" %s %d Monk offset:                  0x%016lX\n",__func__,__LINE__, offset);
    printk(" %s %d Monk pg_offset:               0x%016lX\n",__func__,__LINE__, pg_offset);
    printk(" %s %d Monk vmalloc addr:            0x%016lX\n",__func__,__LINE__, &(vmalloc_area[offset/sizeof(char)]));
    printk(" %s %d Monk vmalloc data:            %s\n",__func__,__LINE__, vmalloc_area);
    printk(" %s %d Monk vmalloc data:            %s\n",__func__,__LINE__, &(vmalloc_area[offset/sizeof(char)]));
    /* translate the vmalloc address to kmalloc address */
#if 0
    //
    virt_kaddr = (unsigned long)vaddr_to_kaddr(&vmalloc_area[offset / sizeof(int)]);
    if (virt_kaddr == 0UL) {
        return VM_FAULT_SIGBUS;
    }
    printk(" %s %d Monk virt_kaddr:              0x%016lX\n",__func__,__LINE__, virt_kaddr);
    vmf->page = virt_to_page(virt_kaddr);
#else
    //
    vmf->page = vmalloc_to_page(&(vmalloc_area[offset/sizeof(char)]));
    if (vmf->page == NULL) {
        printk(" %s %d Monk Failed to vmalloc to page",__func__,__LINE__);
        return VM_FAULT_SIGBUS;
    }
#endif
    get_page(vmf->page);
    return 0;

}
#endif


static struct vm_operations_struct map_vm_ops = {
    .open  = map_vopen,
    .close = map_vclose,
#if (MMAP_TYPE == MMAP_FAULT)
    .fault = map_fault,
#endif
};



/* device open method */
static int mapdrv_open(struct inode *inode, struct file *file)
{
    struct mapdrv *md;
    md = container_of(inode->i_cdev, struct mapdrv, mapdev);
    atomic_inc(&md->usage);
    return 0;
}

/* device close method */
static int mapdrv_release(struct inode *inode, struct file *file)
{
    struct mapdrv* md;
    md = container_of(inode->i_cdev, struct mapdrv, mapdev);
    atomic_dec(&md->usage);
    return 0;
}



#if (ALLOC_TYPE == ALLOC_VMALLOC)
#if (MMAP_TYPE == MMAP_FAULT)
static int mapdrv_mmap_vmalloc_fault(struct file *file, struct vm_area_struct *vma)
{
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

    unsigned long size = vma->vm_end - vma->vm_start;

    printk(" %s %d Monk vma->vm_start: 0x%016lX\n",__func__,__LINE__, vma->vm_start);
    printk(" %s %d Monk vma->vm_end:   0x%016lX\n",__func__,__LINE__, vma->vm_end);
    printk(" %s %d Monk vma->vm_pgoff: 0x%016lX\n",__func__,__LINE__, vma->vm_pgoff);
    printk(" %s %d Monk vma->offset:   0x%016lX\n",__func__,__LINE__, offset);

    if (offset & ~PAGE_MASK) {
        printk(" %s %d Monk offset not aligned: %ld\n",__func__,__LINE__, offset);
        return -ENXIO;
    }
    if (size > MAP_SIZE) {
        printk(" %s %d Monk size too big\n",__func__,__LINE__);
        return -ENXIO;
    }
    /* do not want to have this area swapped out, lock it */
    vma->vm_flags |= VM_LOCKED;
    if (offset == 0) {
        vma->vm_ops = &map_vm_ops;
        /* call the open routine to increment the usage count */
        map_vm_ops.open(vma);
    } else {
        printk(" %s %d Monk offset out of range\n",__func__,__LINE__);
        return -ENXIO;
    }
    return 0;
}
#elif (MMAP_TYPE == MMAP_REMAP)
static int mapdrv_mmap_vmalloc_remap(struct file *file, struct vm_area_struct *vma)
{
    /* vma->vm_pgoff : 页桢号*/
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long virt_start = (unsigned long)vmalloc_area + (unsigned long)(vma->vm_pgoff << PAGE_SHIFT);
    unsigned long pfn_start = (unsigned long)vmalloc_to_pfn((void *)virt_start);
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long vmstart = vma->vm_start;
    int i = 0, ret = 0;

    printk(" %s %d Monk phy:              0x%016lX\n",__func__,__LINE__, pfn_start << PAGE_SHIFT);
    printk(" %s %d Monk offset:           0x%016lX\n",__func__,__LINE__, offset);
    printk(" %s %d Monk size:             0x%016lX\n",__func__,__LINE__, size);

    while (size > 0) {
        ret = remap_pfn_range(vma, vmstart, pfn_start, PAGE_SIZE, vma->vm_page_prot);
        if (ret) {
            printk(" %s %d Monk Failed remap_pfn_range at [0x%016lX  0x%016lX]\n",
                                __func__,__LINE__,vma->vm_start, vma->vm_end);
            return -ENOMEM;
        } else {
            printk(" %s %d Monk map user addr:    0x%016lX\n",__func__,__LINE__, vmstart);
            printk(" %s %d Monk map kernel vaddr: 0x%016lX\n",__func__,__LINE__, virt_start);
            printk(" %s %d Monk map kernel kaddr: 0x%016lX\n",__func__,__LINE__, pfn_start << PAGE_SHIFT);
            printk(" %s %d Monk map index:        %d\n",__func__,__LINE__, i++);
        }

        if (size <= PAGE_SIZE) {
            size = 0;
        } else {
            size       -= PAGE_SIZE;
            vmstart    += PAGE_SIZE;
            virt_start += PAGE_SIZE;
            pfn_start  = vmalloc_to_pfn((void *)virt_start);
        }
    }
    return 0;
}

#endif
#elif (ALLOC_TYPE == ALLOC_KMALLOC)
static int mapdrv_mmap_kmalloc(struct file *file, struct vm_area_struct *vma)
{
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long pfn_start = (virt_to_phys(kmalloc_vaddr) >> PAGE_SHIFT) + vma->vm_pgoff;
    unsigned long size = vma->vm_end - vma->vm_start;
    int ret = 0;

    printk(" %s %d Monk phy:              0x%016lX\n",__func__,__LINE__, pfn_start << PAGE_SHIFT);
    printk(" %s %d Monk offset:           0x%016lX\n",__func__,__LINE__, offset);
    printk(" %s %d Monk size:             0x%016lX\n",__func__,__LINE__, size);

    ret = remap_pfn_range(vma, vma->vm_start, pfn_start, size, vma->vm_page_prot);
    if (ret) {
        printk(" %s %d Monk Failed remap_pfn_range at [0x%016lX  0x%016lX]\n",
                            __func__,__LINE__,vma->vm_start, vma->vm_end);
        return -ENOMEM;
    } else {
        printk(" %s %d Monk map user addr:    0x%016lX\n",__func__,__LINE__, vma->vm_start);
        printk(" %s %d Monk map kernel kaddr: 0x%016lX\n",__func__,__LINE__, pfn_start << PAGE_SHIFT);
    }

    return ret;
}
#elif (ALLOC_TYPE == ALLOC_ALLOC_PAGE)
static int mapdrv_mmap_alloc_page(struct file *file, struct vm_area_struct *vma)
{
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long pfn_start = (virt_to_phys(page_vaddr) >> PAGE_SHIFT) + vma->vm_pgoff;
    unsigned long size = vma->vm_end - vma->vm_start;
    int ret = 0;

    printk(" %s %d Monk phy:      0x%016lX\n",__func__,__LINE__, pfn_start << PAGE_SHIFT);
    printk(" %s %d Monk offset:   0x%016lX\n",__func__,__LINE__, offset);
    printk(" %s %d Monk size:     0x%016lX\n",__func__,__LINE__, size);

    ret = remap_pfn_range(vma, vma->vm_start, pfn_start, size, vma->vm_page_prot);
    if (ret) {
        printk(" %s %d Monk Failed remap_pfn_range at [0x%016lX  0x%016lX]\n",
                            __func__,__LINE__,vma->vm_start, vma->vm_end);
        return -ENOMEM;
    } else {
        printk(" %s %d Monk map user addr:    0x%016lX\n",__func__,__LINE__, vma->vm_start);
        printk(" %s %d Monk map kernel kaddr: 0x%016lX\n",__func__,__LINE__, pfn_start << PAGE_SHIFT);
    }

    return ret;
}
#endif

static struct file_operations mapdrv_fops = {
    .owner   = THIS_MODULE,
#if (ALLOC_TYPE == ALLOC_VMALLOC)
#if (MMAP_TYPE == MMAP_FAULT)
    .mmap    = mapdrv_mmap_vmalloc_fault,
#elif (MMAP_TYPE == MMAP_REMAP)
    .mmap    = mapdrv_mmap_vmalloc_remap,
#endif
#elif (ALLOC_TYPE == ALLOC_KMALLOC)
    .mmap    = mapdrv_mmap_kmalloc,
#elif (ALLOC_TYPE == ALLOC_ALLOC_PAGE)
    .mmap    = mapdrv_mmap_alloc_page,
#endif
    .open    = mapdrv_open,
    .release = mapdrv_release,
};


static int dump_kernel_map_info(void)
{
extern char __init_begin[], __init_end[];
extern char _text[], _etext[];
extern char __start_rodata[], _sdata[], _edata[];

#define MLK(b, t) b, t, ((t) - (b)) >> 10
#define MLM(b, t) b, t, ((t) - (b)) >> 20
#define MLG(b, t) b, t, ((t) - (b)) >> 30
#define MLK_ROUNDUP(b, t) b, t, DIV_ROUND_UP(((t) - (b)), SZ_1K)

    printk(" %s %d Monk Virtual kernel memory layout:\n",__func__,__LINE__);
#ifdef CONFIG_KASAN
    printk(" %s %d Monk    kasan   : 0x%016lX - 0x%016lX   (%6ld GB)\n",__func__,__LINE__,MLM(MODULES_VADDR, MODULES_END));
#endif
    printk(" %s %d Monk    modules : 0x%016lX - 0x%016lX   (%6ld MB)\n",__func__,__LINE__,MLM(MODULES_VADDR, MODULES_END));
    printk(" %s %d Monk    vmalloc : 0x%016lX - 0x%016lX   (%6ld GB)\n",__func__,__LINE__,MLG(VMALLOC_START, VMALLOC_END));
    printk(" %s %d Monk      .init : 0x%p" " - 0x%p" "   (%6ld KB)\n",__func__,__LINE__,MLK_ROUNDUP(__init_begin, __init_end));
    printk(" %s %d Monk      .text : 0x%p" " - 0x%p" "   (%6ld KB)\n",__func__,__LINE__,MLK_ROUNDUP(_text, _etext));
    printk(" %s %d Monk    .rodata : 0x%p" " - 0x%p" "   (%6ld KB)\n",__func__,__LINE__,MLK_ROUNDUP(__start_rodata, __init_begin));
    printk(" %s %d Monk      .data : 0x%p" " - 0x%p" "   (%6ld KB)\n",__func__,__LINE__,MLK_ROUNDUP(_sdata, _edata));
#ifdef CONFIG_SPARSEMEM_VMEMMAP
    printk(" %s %d Monk    vmemmap : 0x%016lX - 0x%016lX   (%6ld GB maximum)\n",__func__,__LINE__,MLG(VMEMMAP_START, VMEMMAP_START + VMEMMAP_SIZE));
    printk(" %s %d Monk              0x%016lX - 0x%016lX   (%6ld MB actual)\n",__func__,__LINE__,MLM((unsigned long)phys_to_page(memblock_start_of_DRAM()), (unsigned long)virt_to_page(high_memory)));
#endif
    printk(" %s %d Monk    fixed   : 0x%016lX - 0x%016lX   (%6ld KB)\n",__func__,__LINE__,MLK(FIXADDR_START, FIXADDR_TOP));
    printk(" %s %d Monk    PCI I/O : 0x%016lX - 0x%016lX   (%6ld MB)\n",__func__,__LINE__,MLM(PCI_IO_START, PCI_IO_END));
    printk(" %s %d Monk    memory  : 0x%016lX - 0x%016lX   (%6ld MB)\n",__func__,__LINE__,MLM(__phys_to_virt(memblock_start_of_DRAM()), (unsigned long)high_memory));
#undef MLK
#undef MLM
#undef MLK_ROUNDUP
    return 0;
}


static int __init mapdrv_init(void)
{
    unsigned long virt_addr;
    int i, result, err;
    dev_t dev = 0;
    struct page *pages = NULL;
    void* page_paddr = 0UL;
    void* page_addr = 0UL;
    void* page_free_vaddr = 0UL;

    void* vmalloc_vaddr = 0UL;
    void* vmalloc_paddr = 0UL;
    void* vmalloc_kaddr = 0UL;

    void* kmalloc_paddr = 0UL;
    void* kmalloc_kaddr = 0UL;

    void *pphyaddr, *pvirtaddr;

    printk(" %s %d Monk PGDIR_SHIFT:   0x%d\n",__func__,__LINE__, PGDIR_SHIFT);
    printk(" %s %d Monk PGDIR_SIZE:    0x%d\n",__func__,__LINE__, PGDIR_SIZE);
    printk(" %s %d Monk PGDIR_MASK:    0x%d\n",__func__,__LINE__, PGDIR_MASK);

    printk(" %s %d Monk PUD_SHIFT:     0x%d\n",__func__,__LINE__, PUD_SHIFT);
    printk(" %s %d Monk PUD_SIZE:      0x%d\n",__func__,__LINE__, PUD_SIZE);
    printk(" %s %d Monk PUD_MASK:      0x%d\n",__func__,__LINE__, PUD_MASK);

    printk(" %s %d Monk PMD_SHIFT:     0x%d\n",__func__,__LINE__, PMD_SHIFT);
    printk(" %s %d Monk PMD_SIZE:      0x%d\n",__func__,__LINE__, PMD_SIZE);
    printk(" %s %d Monk PMD_MASK:      0x%d\n",__func__,__LINE__, PMD_MASK);

    printk(" %s %d Monk PAGE_SHIFT:    0x%d\n",__func__,__LINE__, PAGE_SHIFT);

    printk(" %s %d Monk PTRS_PER_PGD:  0x%d\n",__func__,__LINE__, PTRS_PER_PGD);
    printk(" %s %d Monk PTRS_PER_PUD:  0x%d\n",__func__,__LINE__, PTRS_PER_PUD);
    printk(" %s %d Monk PTRS_PER_PMD:  0x%d\n",__func__,__LINE__, PTRS_PER_PMD);
    printk(" %s %d Monk PTRS_PER_PTE:  0x%d\n",__func__,__LINE__, PTRS_PER_PTE);

    // printk(" %s %d Monk TEXT_OFFSET:   0x%016lX\n",__func__,__LINE__, TEXT_OFFSET);
    printk(" %s %d Monk TASK_SIZE:     0x%016lX\n",__func__,__LINE__, TASK_SIZE);

    printk(" %s %d Monk VA_BITS:       0x%016lX\n",__func__,__LINE__, VA_BITS);
    printk(" %s %d Monk VA_START:      0x%016lX\n",__func__,__LINE__, VA_START);
    printk(" %s %d Monk PAGE_SIZE:     0x%016lX\n",__func__,__LINE__, PAGE_SIZE);
    printk(" %s %d Monk PAGE_OFFSET:   0x%016lX\n",__func__,__LINE__, PAGE_OFFSET);
    printk(" %s %d Monk PHYS_OFFSET:   0x%016lX\n",__func__,__LINE__, PHYS_OFFSET);

    printk(" %s %d Monk KIMAGE_VADDR:  0x%016lX\n",__func__,__LINE__, KIMAGE_VADDR);

    printk(" %s %d Monk VMALLOC_START: 0x%016lX\n",__func__,__LINE__, VMALLOC_START);
    printk(" %s %d Monk VMALLOC_END:   0x%016lX\n",__func__,__LINE__, VMALLOC_END);

    // printk(" %s %d Monk PKMAP_BASE:    0x%016lX\n",__func__,__LINE__, PKMAP_BASE);

    printk(" %s %d Monk FIXADDR_START: 0x%016lX\n",__func__,__LINE__, FIXADDR_START);
    printk(" %s %d Monk FIXADDR_SIZE:  0x%016lX\n",__func__,__LINE__, FIXADDR_SIZE);
    printk(" %s %d Monk FIXADDR_TOP:   0x%016lX\n",__func__,__LINE__, FIXADDR_TOP);

    printk(" %s %d Monk PCI_IO_START:  0x%016lX\n",__func__,__LINE__, PCI_IO_START);
    printk(" %s %d Monk PCI_IO_END:    0x%016lX\n",__func__,__LINE__, PCI_IO_END);
    printk(" %s %d Monk PCI_IO_SIZE:   0x%016lX\n",__func__,__LINE__, PCI_IO_SIZE);

    printk(" %s %d Monk PHY_MEMORY:    0x%016lX\n",__func__,__LINE__, memblock_start_of_DRAM());
    printk(" %s %d Monk HIGH_MEMORY:   0x%016lX\n",__func__,__LINE__, (unsigned long)high_memory);

    printk(" %s %d Monk SWAPPER_INIT_MAP_SIZE:  0x%d\n",__func__,__LINE__, SWAPPER_INIT_MAP_SIZE);
    printk(" %s %d Monk SWAPPER_DIR_SIZE:       0x%016lX\n",__func__,__LINE__, SWAPPER_DIR_SIZE);
    printk(" %s %d Monk IDMAP_DIR_SIZE:         0x%016lX\n",__func__,__LINE__, IDMAP_DIR_SIZE);




    dump_kernel_map_info();

#if 1
    /* __get_free_pages == (alloc_pages + page_address) */
    pages = alloc_pages(GFP_KERNEL,MAP_SIZE/PAGE_SIZE);
    if(!pages){
        printk(" %s %d Failed to alloc page\n",__func__,__LINE__);
        return -ENOMEM;
    }
    page_paddr = (void *)page_to_phys(pages);
    page_addr  = (void *)page_address(pages);
    page_vaddr = (void *)phys_to_virt((unsigned long)page_paddr);
#else
    page_free_vaddr = (void *)__get_free_pages(GFP_KERNEL,MAP_SIZE/PAGE_SIZE);
    page_paddr      = (void *)virt_to_phys(page_free_vaddr);
    page_vaddr      = (void *)phys_to_virt((unsigned long)page_paddr);
#endif

    for(virt_addr = (unsigned long)page_vaddr, i = 0;
        virt_addr < (unsigned long)(&(((char*)page_vaddr)[MAP_SIZE / sizeof(char)]));
        virt_addr += PAGE_SIZE, i++) {
        sprintf((char *)virt_addr, "Kernel vmalloc space ! %d", i);
    }

    printk(" %s %d Monk page_freevaddr   : 0x%016lX\n",__func__,__LINE__, page_free_vaddr);
    printk(" %s %d Monk page_vaddr       : 0x%016lX\n",__func__,__LINE__, page_vaddr);
    printk(" %s %d Monk page_paddr       : 0x%016lX\n",__func__,__LINE__, page_paddr);
    printk(" %s %d Monk page_addr        : 0x%016lX\n",__func__,__LINE__, page_addr);

    /* linear memory logic addr*/
    kmalloc_vaddr = (void*)kmalloc(MAP_SIZE, GFP_KERNEL);
    if (!kmalloc_vaddr) {
        goto fail0;
    }
    for(virt_addr = (unsigned long)kmalloc_vaddr, i = 0;
        virt_addr < (unsigned long)(&(((char*)kmalloc_vaddr)[MAP_SIZE / sizeof(char)]));
        virt_addr += PAGE_SIZE, i++) {
        sprintf((char *)virt_addr, "Kernel vmalloc space ! %d", i);
    }

    kmalloc_paddr = (void*)virt_to_phys((void*)kmalloc_vaddr);
    sprintf((char *)kmalloc_vaddr, "Kernel kmalloc space !");

    printk(" %s %d Monk kmalloc virtaddr : 0x%016lX\n",__func__,__LINE__, kmalloc_vaddr);
    printk(" %s %d Monk kmalloc phyaddr  : 0x%016lX\n",__func__,__LINE__, kmalloc_paddr);

    md = kmalloc(sizeof(struct mapdrv), GFP_KERNEL);
    if (!md) {
        goto fail1;
    }

    result = alloc_chrdev_region(&dev, 0, 1, "mapdrv");
    major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "mapdrv: can't get major %d\n",__func__,__LINE__, major);
        goto fail2;
    }
    minor = MINOR(dev);
    printk(" %s %d Get device %d:%d\n",__func__,__LINE__, major, minor);
    cdev_init(&md->mapdev, &mapdrv_fops);
    md->mapdev.owner = THIS_MODULE;
    md->mapdev.ops   = &mapdrv_fops;
    err = cdev_add(&md->mapdev, dev, 1);
    if (err) {
        printk(KERN_NOTICE "Error %d adding mapdrv", err);
        goto fail3;
    }

    md->map_class  = class_create(THIS_MODULE, "map_test");
    md->map_device = device_create(md->map_class, NULL, dev, NULL, "mapdrv");

    atomic_set(&md->usage, 0);
    /* get a memory area that is only virtual contigous. */
    vmalloc_area = vmalloc(MAP_SIZE);
    if (!vmalloc_area) {
        goto fail4;
    }

    memset(vmalloc_area, 0, MAP_SIZE);
    for(virt_addr = (unsigned long)vmalloc_area, i = 0;
        virt_addr < (unsigned long)(&(vmalloc_area[MAP_SIZE / sizeof(char)]));
        virt_addr += PAGE_SIZE, i++) {
        sprintf((char *)virt_addr, "Kernel vmalloc space ! %d", i);
    }

    pvirtaddr = (void *)vaddr_to_kaddr(vmalloc_area);
    pphyaddr  = (void *)virt_to_phys(pvirtaddr);

    printk(" %s %d Monk  vmalloc_area at 0x%016lX (virt: 0x%016lX, phys: 0x%016lX)\n",__func__,__LINE__, vmalloc_area, pvirtaddr, pphyaddr);

    printk(" %s %d Monk  printk kaddr   : %s\n",__func__,__LINE__, pvirtaddr);
    printk(" %s %d Monk  printk vmalloc : %s\n",__func__,__LINE__, vmalloc_area);
    // printk(" %s %d Monk  printk pphyaddr    : %s\n",__func__,__LINE__, pphyaddr); // illegal !!



    return 0;
fail4:
    cdev_del(&md->mapdev);
fail3:
    unregister_chrdev_region(dev, 1);
fail2:
    kfree(md);
fail1:
    kfree(kmalloc_vaddr);
fail0:
    free_pages((unsigned long)page_paddr, 1);
    return -1;
}

static void __exit mapdrv_exit(void)
{
    dev_t devno = MKDEV(major, 0);

    free_pages((unsigned long)page_vaddr, 1);
    kfree(kmalloc_vaddr);
    /* and free the two areas */
    if (vmalloc_area) {
        vfree(vmalloc_area);
    }
    cdev_del(&md->mapdev);
    /* unregister the device */
    unregister_chrdev_region(devno, 1);
    device_destroy(md->map_class, MKDEV(major, minor));
    class_destroy(md->map_class);
    kfree(md);
}

module_init(mapdrv_init);
module_exit(mapdrv_exit);

MODULE_LICENSE("GPL");