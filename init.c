//初始进程会一直执行main函数

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){   // 控制台设备文件
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();  // 复制出一个子进程
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){ // 创造出的子进程运行shell程序
      exec("sh", argv);   // 执行sh.c程序，开启一个shell进程
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)  // 开启循环调用wait回收僵尸进程
      printf(1, "zombie!\n");
  }
}
