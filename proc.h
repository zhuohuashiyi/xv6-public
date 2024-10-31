// 每一个CPU的定义
struct cpu {
    // apic是高级可编程中断控制器，每一个cpu中必定有一个apic，用于传递中断到处理器
    // 所以这里可以用apic系统的id作为cpu的唯一标识
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  // 该字段表示cpu是否启动
  volatile uint started;       // volatile 关键字表示该字段不需要编译器优化，取值应该从内存中取而不能从缓存中取
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // 表示该cpu上正在运行的进程
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
// 定义上下文结构体，主要保存五个寄存器的值
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;  // 该寄存器为基址指针寄存器
  uint eip;  // 该寄存器保存下一条运行指令的地址
};

// 定义进程的状态，有未使用，新创建，阻塞，就绪，运行，僵死
enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// 进程的结构体定义
struct proc {
  uint sz;                     // 进程内存空间的大小，单位是字节
  pde_t* pgdir;                // Page table 页表指针
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // 进程状态
  int pid;                     // 进程id
  struct proc *parent;         // 指向父进程的指针
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // 表示进程是否已被杀死
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // 进程名，仅用于调试目的
};


// 进程空间是连续存放的，从低地址处开始，依次为存放常量和代码的文本区、数据区、栈、堆。
