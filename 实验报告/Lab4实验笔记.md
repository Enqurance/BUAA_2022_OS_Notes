# Lab4实验笔记

## O、前记

​		lab4的主要内容是**系统调用与fork**。在此之前大致浏览了一下本次实验指导书的内容，应当是以文字阅读和理解为主要内容，故而本次笔记也会有一些这方面的特色。

​		理论上来讲，硬件操作、动态内存分配等操作是被内核认定为比较"危险"的行为，这一类行为放任用户使用可能会造成不可预知的后果，故而它们只能交给内核执行；然而有些用户进程又不可避免地要使用这些操作。故而操作系统会给用户提供"接口"，使得用户能够以安全的方式调用这些内核功能。这就是**系统调用**的初级理解。

​		借用指导书内容，本次实验的主要任务如下：

- 掌握系统调用的概念及流程
- 实现进程间的通讯机制
- 实现进程创建机制`fork()`函数
- 掌握页写入异常的处理流程

## 一、系统调用

### 1.概念回忆

​		首先进行概念的整理，便于后面的学习解释。

|     概念名词      |                             解释                             |
| :---------------: | :----------------------------------------------------------: |
|   内核态/用户态   | CPU运行的两种模式，拥有不同级别的权限。该状态由 CP0协处理器的SR 寄存器中KUc位的值标志 |
| 内核空间/用户空间 | 进程的虚拟地址的两部分（在同一进程中存在）。在虚拟地址映射中，用户空间的虚拟页通过页表映射到物理页，内核空间的虚拟页则映射到固定的物理页和外部设备。CPU在内核态下，才可以访问进程的内核空间。 |
|     进程/内核     | 进程是资源分配和调度的基本单位，内核负责管理和分配系统资源。内核的调度功能决定了它可以和进程共存。 |

​		在lab3中，我们的进程是运行在内核态下的。为了使进程运行在用户态下，我们需要通过某种办法修改CP0中SR寄存器的值（还记得吗？SR低六位寄存器是作为一个二重栈使用的）。我们通过修改`Trapframe`结构体中的值保证进入中断时写入该寄存器的值正确。`exercise 4.0`就是要求在创建进程时向`env_tf.cp0_status`写入0x1000100c。

### 2.系统调用溯源

​		在指导书中，举了一个调用`puts()`函数的例子，来为我们揭示了系统调用的本源。我们知道，标准IO操作必须在中断下进行（也就是内核态），这一过程中就使用到了系统调用。指导书通过一系列汇编、反汇编以及调试操作实现了溯源过程，整理出调用`puts()`函数发挥作用的全过程：

- 调用`puts()`的下层函数`write()`
- `write()`函数为寄存器设定了相应的值，并执行`syscall`
- 系统进入内核态，根据设定的值运行发挥作用
- 返回`write()`函数，取出寄存器值，继续返回直至`puts()`函数

​		这其中有些操作，和我们在组成原理的P7部分完成的工作很是相似（当然，有的同学没有接触P7，指导书需要照顾这些同学所以要写的详细）。我们可以通过这一过程了解到：

- IO操作需要内核来完成，即需要系统进入内核态（当然，不仅仅是IO操作）
- syscall指令可以使系统陷入内核态（在Mars中我们都是用过这个指令）
- 系统状态切换时，需要进行数据保护

​		当今，用户已经很少直接使用系统调用了。很多语言都已经帮我们封装好了一些底层的操作~~，不要重复造轮子~~。

### 3.实现系统调用

​		系统调用也是一种中断，我们在lab3中已经介绍过中断处理流程。异常向量组分发的8号异常就是专门处理这一中断的。指导书指出，我们将在`./user/printf.c`函数中学习相关流程。

```C
static void user_myoutput(void *arg, const char *s, int l)
{
	int i;

	// special termination call
	if ((l == 1) && (s[0] == '\0')) {
		return;
	}

	for (i = 0; i < l; i++) {
		syscall_putchar(s[i]);

		if (s[i] == '\n') {
			syscall_putchar('\n');
		}
	}
}

void writef(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	user_lp_Print(user_myoutput, 0, fmt, ap);
	va_end(ap);
}
```

​		其中的`user_lp_Print()`函数用于输出字符串，它调用了`user_myoutput()`函数；`use_myoutput()`函数又调用了`syscall_putchar()`函数；`syscall_putchar()`函数又调用了`msyscall()`。如果你使用ctag，你就会发现，到此就是调用的终点了，因为`msyscall`就是一个汇编函数，系统在此进入了内核态。结束函数的执行后，会一层层向上返回。

​		如果你细心的话，你会发现在`syscall_putchar()`同文件下，还有很多长相相似的函数，如`syscall_yield()`、`syscall_env_destory()`等。这些函数都会调用`msyscall()`函数，由他们的名字可以知道它们是何种中断触发的系统调用。

​		`msyscall()`传入了六个参数，第一个通常是和本函数对应中断问题相关的宏，学名为系统调用号。剩余的参数随着功能而改变。`msyscall()`传递参数的方法，和组成原理课程中的栈帧操作一样：函数调用时，将当前层函数的内容压栈（通过移动栈指针实现），供下一层函数使用；返回时则推栈（也是移动栈指针）。在`msyscall()`中，我们有六个参数，其中四个可以通过寄存器堆约定俗成的`$a0~$a4`寄存器传递，剩下的要通过栈来传递；不过，在压栈时，我们也会为前四个参数预留栈空间（不写入）。

​		上面都是一些原理性的内容，我们接下来需要在`exercise4.1`中完成`msyscall()`函数。这样的简单操作，我们在上学期已经做的够多了。

```
LEAF(msyscall)
    syscall
    jr  ra
    nop
END(msyscall)
```

​		可以看到汇编语言中的`syscall`指令，此时系统便陷入内核态了。接下来，我们要实现一个`handle_sys`汇编函数。我们上次实现了一个`handle_int`函数用于处理时钟中断，这个函数则是用于处理系统调用中断。这一函数中，我第一次填写的时候尚且有很多地方没能完全弄明白，故而先摘取指导书中的一些解释，后续还会补充我的理解。

​		`syscall`使我们陷入了内核态，在进入内核态之前，我们需要保存当前函数的部分"运行现场"（要注意，陷入内核态并不是函数跳转）。

```C
NESTED(handle_sys,TF_SIZE, sp)
    SAVE_ALL                            /* 用于保存所有寄存器的汇编宏 */
    CLI                                 /* 用于屏蔽中断位的设置的汇编宏 */
    nop
    .set at                             /* 恢复$at寄存器的使用 */
  /* 取出Trapframe的EPC寄存器的值，将其修改为下一条指令的值并写回。由于之前Trapframe结构体已经使用汇编宏保存，此处也只需要使用汇编指令存取 */
	lw		t0, TF_EPC(sp)
	addiu	t0, t0, 4
	sw		t0, TF_EPC(sp)
  /* 将系统调用号存入a0寄存器 */
	lw		a0, TF_REG4(sp)
    addiu   a0, a0, -__SYSCALL_BASE     /* a0 <- “相对”系统调用号 */
    sll     t0, a0, 2                   /* t0 <- 相对系统调用号 * 4 */
    la      t1, sys_call_table          /* t1 <- 系统调用函数的入口表基地址 */
    addu    t1, t1, t0                  /* t1 <- 特定系统调用函数入口表项地址 */
    lw      t2, 0(t1)                   /* t2 <- 特定系统调用函数入口函数地址 */
		/* 经过上面的一系列操作，成功将系统调用号对应的函数地址放入了t2寄存器 */
    lw      t0, TF_REG29(sp)            /* t0 <- 用户态的栈指针 */
    lw      t3, 16(t0)                  /* t3 <- msyscall的第5个参数 */
    lw      t4, 20(t0)                  /* t4 <- msyscall的第6个参数 */
    /* 使用栈指针为六个参数分配空间，并将参数写入正确的位置。最后两个参数只能使用内存传递值；其余四个参数可以使用a组寄存器传值 */
		lw		a0, TF_REG4(sp)
		lw		a1, TF_REG5(sp)
		lw		a2, TF_REG6(sp)
		lw		a3, TF_REG7(sp)
  	/* 栈向下增长，并且将第五个和第六个参数写入寄存器，传入下一层函数；其余的参数通过参数寄存器传递 */
		addiu	sp, sp, -24
		sw		t3, 16(sp)
		sw		t4, 20(sp)

    jalr    t2                          // Invoke sys_* function
    nop

  	/* 恢复栈指针 */
		addiu	sp,	sp,	24
    sw      v0, TF_REG2(sp)            /* 将$v0中的sys_*函数返回值存入Trapframe */
    j       ret_from_exception         /* 从异常中返回（恢复现场） */
    nop
END(handle_sys)
```

### Thinking 4.1

- 保存现场时，内核会使用一个汇编宏函数`SAVE_ALL`，将相关通用寄存器的值保存到栈帧中。
- 可以，寄存器的值未被改动。
- 通过寄存器传递了四个参数，通过栈帧传递了了两个参数至函数的位置。
- 修改了`Trapframe`中`EPC`的值为下一条指令的地址，确保指令执行正确。

### 4.3系统调用函数

​		我们已经能够调用相关的系统调用函数了。接下来，我们将补充几个系统调用函数，以完善系统调用机制。

​		首先是`sys_mem_alloc()`函数。这个函数将会为进程号为`envid`的进程分配一页空间。

```C
int sys_mem_alloc(int sysno, u_int envid, u_int va, u_int perm)
{
    // Your code here.
    struct Env *env;
    struct Page *ppage;
    int ret;
    ret = 0;
  	/* 判断权限与va是否合法 */
    if(perm & PTE_COW) return -E_INVAL;
    if(((perm & PTE_V) == 0) || (va >= UTOP)) return -E_INVAL;
  	/* 安全地声明一个页面 */
    if((ret = page_alloc(&ppage)) < 0) return ret;
  	/* 安全地得到envid对应的进程 */
    if((ret = envid2env(envid, &env, 0)) < 0) return ret;
  	/* 安全地将该页面插入到进程的页目录中 */
    if((ret = page_insert(env->env_pgdir, ppage, va ,perm)) < 0) return ret;
    return ret;
}
```

​		然后是`sys_mem_map()`函数，用于将一个进程的某个虚拟地址对应的页面和另一个进程某个虚拟地址对应的页面关联起来，即共享同一个物理页面。

```C
int sys_mem_map(int sysno, u_int srcid, u_int srcva, u_int dstid, u_int dstva,
                u_int perm)
{
    int ret;
    u_int round_srcva, round_dstva;
    struct Env *srcenv;
    struct Env *dstenv;
    struct Page *ppage;
    Pte *ppte;

    ppage = NULL;
    ret = 0;
  	/* 向下取整，保证对齐 */
    round_srcva = ROUNDDOWN(srcva, BY2PG);
    round_dstva = ROUNDDOWN(dstva, BY2PG);

    //your code here
  	/* 判断权限和va是否合法 */
    if((perm & PTE_V) == 0) return -E_INVAL;
    if((srcva >= UTOP) || (dstva >= UTOP)) return -E_INVAL;
  	/* 根据envid寻找目标进程 */
    if((ret = envid2env(srcid, &srcenv, 0)) < 0) return ret;
    if((ret = envid2env(dstid, &dstenv, 0)) < 0) return ret;
  	/* 在源进程的目录下寻找源虚拟地址对应的页面 */
    ppage = page_lookup(srcenv->env_pgdir, round_srcva, &ppte);
  	/* 判断寻找结果与权限 */
    if(ppage == NULL) return -E_INVAL;
    if((ppte != NULL) && ((perm & PTE_R) == 1) && ((*ppte & PTE_R) == 0)) return -E_INVAL;
  	/* 将寻找到的页面插入目标进程的页目录中 */
    ret = page_insert(dstenv->env_pgdir, ppage, round_dstva, perm);
    return ret;
}
```

​		下面一个函数是`sys_mem_unmap()`，其作用是解除某个虚拟地址在某个进程当中与某个物理页面的映射关系。

```C
int sys_mem_unmap(int sysno, u_int envid, u_int va)
{
    int ret;
    struct Env *env;

    ret = 0;
		/* 判断va是否合法 */
    if(va >= UTOP) return -E_INVAL;
  	/* 安全地寻找目标进程 */
    if((ret = envid2env(envid, &env, 0)) < 0) return ret;
    /* 解除虚拟地址va的映射 */
  	page_remove(env->env_pgdir, va);
    return ret;
}
```

​		最后一个函数是`sys_yield()`，它使得当前进程放弃CPU，并且进行进程调度。

```C
void sys_yield(void)
{
  	/* 保护进程现场 */
    bcopy((void *)KERNEL_SP - sizeof(struct Trapframe),
          (void *)TIMESTACK - sizeof(struct Trapframe),
          sizeof(struct Trapframe));
  	/* 调用进程调度函数 */
    sched_yield();
}
```

​		相比于**实现系统调用**的部分，这几个系统调用函数的作用和实现方法都更好理解。了解并且能够正确使用在前几个lab中写好的函数，应当能够顺利完成；如果对某些函数的功能有所遗忘，还需回头温习。

## 二、进程通信IPC

​		在微内核系统中，进程的一些相关功能如文件系统、驱动等都被移出内核。为了让不同的进程能够相互沟通、请求资源，人们实现了IPC机制。IPC机制有如下的要点：

- 实现两个进程的通讯
- 通过系统调用发挥作用
- 以页为基础交换数据

​		我们知道，每一个进程都有自己的虚拟地址空间，页号相同的的虚拟页面可能映射不同的物理页面。故而实现进程通信的关键在于让两个不同的进程读取到同样的物理页面。顺着这一思路寻找，我们会发现不同的进程其实还是拥有同一片空间的，那就是内核空间。这是因为每一个进程在初始化时，它们的地址空间在内核区域的映射都是统一的。故而，我们可以利用内核空间实现IPC。具体的来说，就是：

- 数据发出进程在系统调用前将需要传输的数据放入内核空间
- 数据接收进程通过系统调用从内核空间读取数据

​		为了配合系统调用实现IPC机制，我们在进程控制块中添加了一些域，便于标记进程状态、保存沟通信息。

|      域名       |                           功能                           |
| :-------------: | :------------------------------------------------------: |
|  env_ipc_value  |                    进程传递的具体数值                    |
|  env_ipc_from   |                      发送方的进程ID                      |
| env_ipc_recving | 进程状态标记，1表示进程等待接受数据中，0表示不可接受数据 |
|  env_ipc_dstva  |       接收到的页面需要与自身的哪个虚拟页面完成映射       |
|  env_ipc_perm   |                  传递的页面的权限位设置                  |

​		很显然，我们在进行系统调用时，需要对这些值进行设置，然后陷入内核态。从本质上来讲，IPC机制也是系统调用的一种。完成这一机制，需要实现两个系统调用函数，即`sys_ipc_recv()`和`sys_ipc_can_send()`。

```C
void sys_ipc_recv(int sysno, u_int dstva)
{
  	/* 判断dstva是否合法，dstva是本进程需要映射到页面 */
    if(dstva >= UTOP) return;
    if(curenv != NULL){ 
      	/* 设置进程状态为可接受数据 */
        curenv->env_ipc_recving = 1;
      	/* 设置进程接收数据的虚拟地址 */
        curenv->env_ipc_dstva = dstva;
      	/* 设置进程运行状态为阻塞态，即需要等待数据 */
        curenv->env_status = ENV_NOT_RUNNABLE;
      	/* 调用调度算法，进程开始等待其他进程传输数据 */
        sys_yield();
        //Env;
    }
}

int sys_ipc_can_send(int sysno, u_int envid, u_int value, u_int srcva,
                     u_int perm)
{

    int r;
    struct Env *e;
    struct Page *p;
    struct Pte *ppte;
    if(srcva >= UTOP) return -E_INVAL;
  	/* 安全地获得目标进程，即envid对应的进程 */
    if((r = envid2env(envid, &e, 0)) < 0) return r;
  	/* 目标进程不能接受信息，返回错误 */
    if(e->env_ipc_recving != 1) return -E_IPC_NOT_RECV;
  	/* 设置目标进程控制块相应值 */
    e->env_ipc_value = value;					//设置value，一个可传递的值
    e->env_ipc_from = curenv->env_id;	//设置目标进程的接收进程id为curenv的id
    e->env_ipc_recving = 0;						//设置目标进程为不可接受，因为其即将完成数据接收
    e->env_ipc_perm = perm;						//设置页面权限
    e->env_status = ENV_RUNNABLE;			//设置目标进程为就绪态，即唤醒该进程
    //if(srcva >= UTOP) return -E_INVAL;
    /* 源地址不为零，则需要进行页面映射 */
  	if(srcva != 0) {
      	/* 根据虚拟地址，在当前进程的页目录中寻找目标页 */
        p = page_lookup(curenv->env_pgdir, srcva, &ppte);
        if(p == 0) return -E_INVAL;
      	/* 将找到的页面根据虚拟地址插入到目标进程的页目录中 */
        if((r = page_insert(e->env_pgdir, p, e->env_ipc_dstva, perm)) < 0) return r;
    }
    return 0;
}
```

​		搬用指导书的图片，进程通信的流程大致如下：

![IPC流程图](https://os.buaa.edu.cn/assets/courseware/v1/c9e0dfc9135d81f1e6e78ece6dbd7e6c/asset-v1:BUAA+B3I062270+2022_SPRING+type@asset+block/4-ipc.png)

​		此图很好地表现了进程通信过程中系统调用发挥的作用。

### Thinking 4.2

​		0在MOS中用于表示当前进程，是为`curenv`专门保留的一个位置。在**系统调用**和**IPC**两部分中，很多函数都传入了`envid`；除此之外，我们要注意到，在我们使用系统调用时，很多情况下都是在一个进程和内核之间进行往返。为了便于识别进程，保留一个0用于表示当前进程是很方便的。在`envid2env()`函数中，识别到`envid`为0就直接得到`curenv`，毋需再从数组中存取。

## 三、Fork

### 1.基本认知

​		fork，本质上也是系统调用的一种。在Linux环境下，其被封装成一个库函数API，可以在高级语言中直接调用。关于fork功能的最简单的描述是：某一个进程通过fork创建一个新的进程，且新的进程在被创建时有着和原来的进程相同的上下文环境。

​		某一进程调用fork函数后，该进程将会在宏观上发生"分岔"，即由此伸出了两个进程——一个父进程，一个子进程。在两个进程中，fork函数的返回值并不相同：在父进程中返回子进程的id，在子进程中返回0。

```C
#include <stdio.h>
#include <unistd.h>

int main() {
	int var = 1;
	long pid;
	printf("Before fork, var = %d.\n", var);
	pid = fork();
  /* 在fork()函数后，已经存在两个不同的进程 */
	printf("After fork, var = %d.\n", var);
  /* 子进程中，pid为0，会执行第一个if判断 */
  /* 父进程会执行第二个判断。var两个进程中独立 */
	if (pid == 0) {
		var = 2;
		sleep(3);
		printf("child got %ld, var = %d", pid, var);
	} else {
		sleep(2);
		printf("parent got %ld, var = %d", pid, var);
	}
	printf(", pid: %ld\n", (long) getpid());
	return 0;
}
```

### Thinking 4.3

​		子进程仅执行了fork函数后的代码，没有执行fork函数前的代码，可以猜测子进程可能和父进程共享代码段，也可以猜测子进程在被系统调用创建后，系统调用令子进程回到了fork函数结束的位置，继续执行代码。

### Thinking 4.4

​		关于 fork 函数的两个返回值，下面说法正确的是：

​		**C、fork 只在父进程中被调用了一次，在两个进程中各产生一个返回值**

​		在这一部分中，我们有很多函数需要填写。我会首先独立地介绍每个函数的功能，随后将会归纳总结他们之间的调用关系。

### 2.写时复制机制

​		在前面的思考题当中，我们猜测父子进程可能共享代码段。实际上不仅仅如此，父子进程甚至共用了物理内存。子进程的代码段、数据段、堆栈都映射到了父进程中相同区段对应的页面。

​		我们学过多线程和并发，既然不同的进程共享了相同的物理页面，那么会不会在读写的时候产生数据竞争呢？这么想是没有错的，因为这种情况的确可能发生。但在介绍解决方法前，我们先关注fork函数的一些特征。我们使用fork函数的时候，可能不总是希望它个exce函数一样创建一个全新的子进程，而是希望子进程延用父进程的一些东西。所以我们并不会选择每一次调用fork函数就将父进程的所有数据照搬一遍，而是会令它们共享某些数据，这样可以节约空间和时间。那么，假如在没有调用exce的情况下也出现了修改物理内存的情况怎么办？这时候我们就需要写时复制机制了。

​	写时复制机制的实现思路大致如下：在执行fork函数之后，内核在给子进程配置环境的时候，会将共享页面的`PTE_COW`权限置为有效。随后进程执行的时候，倘若仅对这些页面进行读操作，那么会被认为是安全的；倘若某个进程对他们进行了写操作，那么就会产生异常，进入异常处理函数。在异常处理函数中，内核会解除原来虚拟页面的映射，并将其映射到一个新的物理页面，将原物理页面的内容拷贝到新页面中，同时取消虚拟页面的`PTE_COW`标记。写时复制机制保证仅当进程修改共享页面时才重新分配物理页面，能够节省相当的空间和时间。

​		
