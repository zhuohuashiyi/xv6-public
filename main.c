#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers(void);
static void mpmain(void)  __attribute__((noreturn));
extern pde_t *kpgdir;
extern char end[]; // first address after kernel loaded from ELF file

// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
// main函数为entry后的执行函数
int
main(void)
{ // 以下完成各种初始化工作
  kinit1(end, P2V(4*1024*1024)); // phys page allocator
  kvmalloc();      // kernel page table
  mpinit();        // 检测其他处理器，函数在mp.c文件中
  lapicinit();     // interrupt controller
  seginit();       // segment descriptors
  picinit();       // disable pic
  ioapicinit();    // another interrupt controller
  consoleinit();   // console hardware
  uartinit();      // serial port
  pinit();         // 进程块表初始化
  tvinit();        // trap vectors
  binit();         // buffer cache
  fileinit();      // file table
  ideinit();       // disk 
  startothers();   // 开启其他处理器
  kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); // must come after startothers()
  userinit();      // 创建第一个用户进程初始进程，该进程永远不会退出
  mpmain();        // 开启调度器进程
}
 
// 其他cpu在entryother.S中完成了所有初始化工作后将进入该函数进一步初始化
static void
mpenter(void)
{
  switchkvm();
  seginit();
  lapicinit();
  mpmain(); // 开启调度器进程
}

// 所有CPU都会运行该函数以开启调度器进程开始正常调度用户进程运行
static void
mpmain(void)
{
  cprintf("cpu%d: starting %d\n", cpuid(), cpuid());
  idtinit();       // load idt register
  // started这段表征cpu已经跑起来了
  xchg(&(mycpu()->started), 1); // tell startothers() we're up
  scheduler();     // 开启调度器进程这一个内核级的进程，该进程永远不会退出
}

pde_t entrypgdir[];  // For entry.S

// 启动其他主CPU
static void
startothers(void)
{
  extern uchar _binary_entryother_start[], _binary_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;

  // Write entry code to unused memory at 0x7000.
  // The linker has placed the image of entryother.S in
  // _binary_entryother_start.
  code = P2V(0x7000);  // 物理地址转换为虚拟地址
  // 由于链接器已经将entryother.S的入口地址复制给了符号_binary_emtryother_start
  // 所以这一步将entryother.S的代码复制到物理地址0x7000处
  memmove(code, _binary_entryother_start, (uint)_binary_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == mycpu())  // 主cpu不应该参与以下的操作
      continue;

    // Tell entryother.S what stack to use, where to enter, and what
    // pgdir to use. We cannot use kpgdir yet, because the AP processor
    // is running in low  memory, so we use entrypgdir for the APs too.
    stack = kalloc();
    // 以下操作将栈、页表等信息存放在code(0x7000)往下的12个字节
    *(void**)(code-4) = stack + KSTACKSIZE; // 栈的顶部地址
    *(void(**)(void))(code-8) = mpenter;   // 执行完entryother.S后跳转到这一个地址继续执行
    *(int**)(code-12) = (void *) V2P(entrypgdir);  // 临时页表地址

    lapicstartap(c->apicid, V2P(code)); // 使该cpu开始执行code（0x7000)处的代码

    // 等待CPU c启动完成，在开始启动下一个cpu
    while(c->started == 0)
      ;
  }
}

// The boot page table used in entry.S and entryother.S.
// Page directories (and page tables) must start on page boundaries,
// hence the __aligned__ attribute.
// PTE_PS in a page directory entry enables 4Mbyte pages.

__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDENTRIES] = {
  // Map VA's [0, 4MB) to PA's [0, 4MB)
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
  // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
  [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
};

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

