#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// 以下是进程表定义，NPROC=64，所以最多只能有64个进程
struct {
  struct spinlock lock;  // 进程锁，保证并发访问该表的安全性
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;  // 初始进程

int nextpid = 1;
// extern 关键字表示该方法或者变量在后文定义
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// 主要是初始化进程块表中的锁
void
pinit(void) 
{
  initlock(&ptable.lock, "ptable");
}

// 必须在关闭中断的情况下调用该函数
// cpus是保存系统所有cpu的数组
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
// mycpu函数返回指向当前cpu的指针
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  // 根据apicid找到相应的cpu
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}


// myproc函数返回当前cpu正在执行的进程指针
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  // 关闭中断调用
// 避免从cpu结构中读取proc时被调度
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
// 从进程表中寻找一个未使用的进程块
// 如果找到则改变该进程块的状态为新生态且初试化
// 否则的话返回0
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;
  // 获取进程表的锁，保证并发安全
  acquire(&ptable.lock);
  // 以下遍历进程表寻找可用的进程块
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found; // 找到的话跳转到found处执行
      // goto语法不利于程序的局部性原理，不推荐使用

  release(&ptable.lock);
  return 0;  // 返回0表示找不到可用的进程块

found:
  p->state = EMBRYO;  // 修改进程的状态为新生态
  p->pid = nextpid++;  // 分配进程id,锁的操作保证进程id不会重复分配

  release(&ptable.lock);

  // 分配内核栈
  // 以下栈空间分配失败，必须释放该进程块
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;  // 栈往下增长
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;  // 在栈中保存该进程的上下文信息
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);  // 全部初始化为零
  p->context->eip = (uint)forkret;   // 进程第一次调度一定执行forkret函数

  return p;  // 创建进程成功返回指向进程的指针
}

//PAGEBREAK: 32
// 创建第一个用户进程
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();  // 分配一个进程块及完成内核栈的分配和初始化工作
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)  // 分配页表内核空间
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size); // 初始化页目录等
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));  // 初始化陷入帧为零
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // 初始进程执行initcode.S程序

  safestrcpy(p->name, "initcode", sizeof(p->name));  // 设置进程名
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;   // 通过锁保证安全地设置该进程的状态为就绪状态

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
// 给本进程增加或者缩小n字节的内存空间
// 返回0表示成功，-1表示失败
int
growproc(int n)
{
  uint sz;
  // 首先获取指向本进程的指针
  struct proc *curproc = myproc();

  sz = curproc->sz;  // 当前进程拥有的空间大小
  // 根据n的正负决定是扩容还是缩容
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
// 复制当前进程创造一个新进程，并以当前进程为父进程
// 返回-1表示fork失败，负责返回的是子进程的进程id
int
fork(void)
{
  int i, pid;
   struct proc *np; // 子进程
  struct proc *curproc = myproc(); // 获取当前进程指针

  // 为子进程从进程表中分配一个可用的进程块
  if((np = allocproc()) == 0){  // 返回0表示分配进程块失败
    return -1;
  }

  // 复制当前进程的页表给子进程
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;  // 设置子进程的父进程为当前进程
  *np->tf = *curproc->tf;

  // 这一步是为了子进程中的fork会返回0
  np->tf->eax = 0;
  // 子进程文件相关设置
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));  // 复制当前进程的进程名给子进程

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;  // 设置子进程状态为就绪

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
// exit函数退出当前进程，但不会回收这些进程，这些进程会成为僵尸进程，直到他们的父进程调用wait函数
// exit将自动调用sche函数切换进程放弃自己占用的cpu
void
exit(void)
{
  struct proc *curproc = myproc();  // 获取当前进程的指针
  struct proc *p;
  int fd;

  if(curproc == initproc)   // 初始进程不应该退出
    panic("init exiting");

  // 关闭所有打开的文件
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }
  // 以下是文件相关操作
  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  // 唤醒可能被阻塞的父进程
  wakeup1(curproc->parent);

  // 因为当前进程将要退出，所以其所有子进程将成为孤儿进程
  // 这样就出现了一个问题，这些孤儿进程如果成为僵尸进程的话没有进程会去回收执行进程
  // 这些孤孤儿进程将一致占用进程块等资源
  // 为了解决上述问题，xv6采取了和linux相同的策略，即使初始进程收养当前进程的所有子进程
  // 而初始进程永远不会退出，保证了孤儿进程有进程管理
  // 并且如果这些子进程中有僵尸进程，则唤醒可能被阻塞的初始进程
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;  // 设置进程的状态，注意此时锁没有被释放，保证了并发的安全性
  sched();   // 调度进程
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
// wait函数等待一个子进程退出
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();  // 获取当前进程
  
  acquire(&ptable.lock);
  for(;;){
    // 死循环遍历进程表
    havekids = 0; // 表征当前进程是否还有子进程
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){  // 找到一个处于僵尸状态的子进程，回收其占有的相关资源
        // Found one.
        pid = p->pid;
        kfree(p->kstack);  // 释放子进程的内核栈空间
        p->kstack = 0;
        freevm(p->pgdir);  // 释放子进程的页表指针
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;  // 释放子进程的进程块
        release(&ptable.lock);
        return pid;  // 返回被回收的子进程的id
      }
    }

    // 如果当前进程没有子进程或者当前进程被杀死的话则提出该函数
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    // 自我阻塞，在exit函数中有对应的唤醒操作
    sleep(curproc, &ptable.lock);  
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
// 进程调度函数，永远不返回，循环进行以下操作
// 选择一个状态为Runnable的进程运行
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();  // 获取当前进程
  c->proc = 0;
  
  for(;;){
    // 开启本处理器上的中断
    sti();

    // 获取进程块表锁，如果锁被占用，则阻塞
    // 这保证了并发的安全性
    acquire(&ptable.lock);
    // 以下开始从进程块表中寻找就绪的进程
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      // 当前p指向的进程成为新的运行进程
      c->proc = p;
      switchuvm(p);  // 切换页表等设置为该进程的页表
      p->state = RUNNING;
      // 调用汇编程序进行进程，从调度器程序切换到选中待运行程序
      swtch(&(c->scheduler), p->context);
      switchkvm();
      // 当选中的进程运行结束的时候，其自己应该改变自己状态
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    // 释放进程块表的锁
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
// 运行结束的进程调用该函数切换到调度器进程执行
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))  // 当前进程必须持有锁
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)  // 当前进程必须在调用该函数前改变自己的进程状态
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);  // 切换到调度器进程调度一个就绪进程继续运行
  mycpu()->intena = intena;
}


// yield函数自动放弃cpu，调用sched函数进行进程调度
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;  // 当前进程并没有被阻塞，只是自动放弃cpu，其状态应改为就绪态
  sched();
  release(&ptable.lock);
}

// fork创建的子进程第一次被调度将会执行该函数，从而返回到用户态
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// 进程自我阻塞函数
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING; // 改变进程的状态为阻塞

  sched();  // 切换到调度程序

  // Tidy up.  // 该进程被唤醒后
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
// wakeup1唤醒所有相关进程
static void
wakeup1(void *chan)
{
  struct proc *p;
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  //  唤醒所有阻塞在chan上的进程，将其状态改为就绪态
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
// kill函数杀死指定id的进程
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;  // 表征该进程已经被杀死
      // Wake process from sleep if necessary.
      // 如果被杀死的进程处于阻塞态，应唤醒使其执行必要的操作，然后被回收
      if(p->state == SLEEPING)
        p->state = RUNNABLE; 
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
//  debug使用
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
