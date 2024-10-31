# 进程切换由汇编语言实现
#   以下是外部C语言调用进程切换的函数形式
#   old指示旧进程的上下文的地址，传入二级指针是因为为了让old指向压入切换上下文后的栈顶
#   new指向待切换的就绪进程的上下文地址
#   void swtch(struct context **old, struct context *new);
# 
# Save the current registers on the stack, creating
# a struct context, and save its address in *old.
# Switch stacks to new and pop previously-saved registers.
# 进程切换，主要是上下文切换
.globl swtch   #  global伪操作使得符号对链接器可见，且可以声明为全局函数，为外部可见
swtch:
# esp寄存器指示的是栈顶的地址，所以esp栈顶为返回地址，
# esp+4为旧进程上下文old的地址，esp+8为新进程上下文new的地址
# 相同于把old和new这两个入参暂时保存到了eax和edx寄存器中
  movl 4(%esp), %eax
  movl 8(%esp), %edx

  # Save old callee-saved registers
  # 将旧进程的上下文压入栈中保存起来 
  # 但查看上下文结构体的定义还有一个eip没有被保存，
  # 因为eip指示的是旧进程发起进程切换返回后要执行的下一条指令的地址，实际上就是已经被保存在栈中的返回地址
  pushl %ebp
  pushl %ebx
  pushl %esi
  pushl %edi

  # Switch stacks
  # 把此时栈顶的地址复制到eax指向的地址处，之前将old值保存到了eax寄存器中，这一步相当于*old=esp
  # 即old指向处变为旧进程上下文所在栈的栈顶地址
  movl %esp, (%eax)  
  movl %edx, %esp  # 将new参数复制到esp寄存器中，这样此时栈顶保存的就是新进程上下文

  # 从栈顶依次取出各个寄存器的值
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret  # 函数返回
