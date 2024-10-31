# Initial process execs /init.
# This code runs in user space.

#include "syscall.h"
#include "traps.h"


# 执行init.c程序
.globl start   #  初始进程入口
start:
  pushl $argv  # 压入参数
  pushl $init  # 压入可执行文件的路径
  pushl $0  // where caller pc would be
  movl $SYS_exec, %eax
  int $T_SYSCALL  # 执行init程序

#  循环运行init程序
exit:
  movl $SYS_exit, %eax
  int $T_SYSCALL
  jmp exit

# char init[] = "/init\0";
init:   # $init的定义
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:  # $argv的定义
  .long init
  .long 0

