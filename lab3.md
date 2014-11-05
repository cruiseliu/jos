Part A
======

### Exercise 1

为 Env (PCB) 分配空间:

    envs = boot_alloc(NENV * sizeof(struct Env));
    memset(envs, 0, NENV * sizeof(struct Env));

使其对用户可见:

    boot_map_region(kern_pgdir, UENVS, PTSIZE, PADDR(envs), PTE_U);

### Exercise 2

`env_init()` 初始化 PCB, 并要求链表的顺序为 0, 1, 2, ...

    int i;
    for (i = NENV - 1; i >= 0; i--) {
        envs[i].env_link = env_free_list;
        env_free_list = &envs[i];
    }

`env_setup_vm()` 分配页目录, 其内容可以直接从内核的页目录复制

    e->env_pgdir = page2kva(p);
    inc_ref(p); // p->pp_ref++
    memcpy(e->env_pgdir, kern_pgdir, PGSIZE);

`region_alloc()` 为一个进程分配物理内存, 要求能正确处理边界条件, 并且建议支持未对齐的内存地址和长度. 所谓边界条件指的应该是 0xfffff000 + PGSIZE = 0 < va + len

    len = (ROUNDUP(va + len, PGSIZE) - ROUNDDOWN(va, PGSIZE));
    va = ROUNDDOWN(va, PGSIZE);
    int i;
    for (i = 0; i < len; i+= PGSIZE) {
        PageInfo *p = page_alloc(ALLOC_ZERO);
        if (!p || page_insert(e->env_pgdir, p, va + i, PTE_W | PTE_U) < 0)
            panic("Out of memory\n");
    }

`load_icode()` 用于加载一个用户程序, 和 bootloader 加载内核的过程非常相似. 比较容易忘记的是初始化 PCB 的 eip, 另外如果想直接使用用户环境的虚拟地址那么这里就要提前 lcr3 了.

    struct Elf *elf = (struct Elf *)binary;
    if (elf->e_magic != ELF_MAGIC)
        panic("Not an ELF file!\n");
    
    struct Proghdr *ph = (struct Proghdr *) &binary[elf->e_phoff];
    struct Proghdr *eph = ph + elf->e_phnum;
    
    // make it easier to use p_va
    lcr3(PADDR(e->env_pgdir));
    
    for (; ph < eph; ph++) {
        if (ph->p_type != ELF_PROG_LOAD) continue;
    
        region_alloc(e, (void*)ph->p_va, ph->p_memsz);
        memcpy((void*)ph->p_va, &binary[ph->p_offset], ph->p_filesz);
        memset((void*)ph->p_va + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
    }
    
    e->env_tf.tf_eip = elf->e_entry;
    
    region_alloc(e, (void*)(USTACKTOP - PGSIZE), PGSIZE);

`env_create()` 创建第一个用户进程

    struct Env *e;
    if (env_alloc(&e, 0) < 0)
        panic("Failed to create env!\n");
    load_icode(e, binary);
    e->env_type = type;

`env_run()` 启动指定的用户进程. 因为加载时已经 lcr3 过了, 所以这里可以省略

    if (e != curenv) {
        if (curenv && curenv->env_status == ENV_RUNNING)
            curenv->env_status = ENV_RUNNABLE;
        e->env_status = ENV_RUNNING;
        e->env_runs++;
        curenv = e;
    }
    env_pop_tf(&curenv->env_tf);

### Exercise 3

长得要死

~~好像不读完也能做~~

### Exercise 4

按照要求 _alltraps 如下:

    pushl %ds
    pushl %es
    pushal
    
    movw $GD_KD, %ax
    movw %ax, %ds
    movw %ax, %es
    
    pushl %esp
    call trap

`trap_init` 和 `TRAP_HANDLER` 的定义放到 Challenge 1 吧

### Challenge 1

本来想抄 xv6, 结果发现别人是用脚本生成的代码...

题目里提示了利用 `.text` 和 `.data`, 汇编器会自动把 `.text` 合成一坨, `.data` 合成另一坨, 即使它们在代码中是交替出现的. 所以我们可以方便地在 `.data` 段生成一个函数指针的数组, 这样就能用循环来注册 handler 了.

首先, `TRAPHANDLER` 和 `TRAPHANDLER_NOEC` 长得太像了, 不符合 DRY 原则, 可以合成一个:

    #define PUSHEC(ec) PUSHEC_ ## ec
    #define PUSHEC_1
    #define PUSHEC_0 pushl $0
    
    #define TRAPHANDLER(num, ec) \
        .data; \
            .long vector ## num; \
        .text; \
            .globl vector ## num; \
        vector ## num: \
            PUSHEC(ec); \
            pushl $(num); \
            jmp _alltraps

然后定义函数指针数组, 图省事这里直接写死了32位指针

    .data
        .align 4
        .globl vector
    vector:

定义 handler 的代码又长又没营养就不贴了. 这里应该也是有办法避免 "repeat yourself" 的, 但是感觉可读性实在太差了, 所以就直接把每个陷阱都列出来了. 翻手册可以知道有 error code 的是 `T_DBLFLT`, `T_CORPOC`, `T_TSS`, `T_SEGNP`, `T_STACK`, `T_GPFLT`, `T_RES`, 以及 `T_ALIGN`.

最后在 `trap_init()` 中注册, `T_BRKPT` 和 `T_SYSCALL` 用户可以使用, 其他全部只能由内核使用

    extern void (*vector[])();
    int i;
    for (i = 0; i <= 19; i++)
        SETGATE(idt[i], 0, 8, vector[i], i == T_BRKPT ? 3 : 0);
    SETGATE(idt[T_SYSALL], 0, 8, vector[T_SYSCALL], 3);

### Questions 1

 1. x86 硬件没有压入中断号, 所以如果使用一个 handler 就无法判断发生的是什么中断.
 
 2. 因为用户程序没有权限使用 int 14 (Page Fault), 所以产生了一个 General Protection 中断. 如果允许将会少压入一个 error code.

Part B
======

### Exercise 5

非常简单

    switch (tf->tf_trapno) {
        case T_PGFLT:
            page_fault_handler(tf);
            return;
    }

### Exercise 6

和前面一样简单, 为了 challenge 可以把 T_DEBUG 也加上

    case T_BRKPT:
    case T_DEBUG:
        monitor(tf);
        return;

### Challenge 2

在 monitor 中加入 continue, step, si 等函数, 写起来都大同小异. 以 continue 为例:

    int mon_continue(int argc, char **argv, struct Trapframe *tf)
    {
        if (!tf | (tf->tf_trapno != T_BRKPT && tf->trapno != T_DEBUG)) {
            cprintf("No breakpoint found\n");
            return 1;
        }
        
        tf->tf_eflags &= ~FL_TF;
        
        // should never return
        env_run(curenv);
        
        cprintf("Failed to continue program\n");
        return 2;
    }

其中 `tf->tf_eflags &= ~FL_TF` 关闭 Trap Flag, 也就是取消单步. 对应的 si 中则应该是 `tf->tf_eflags |= FL_TF`. step 则可以用 debuginfo_eip 获取行号, 然后自动 si 到行号变化为止.

我看了一下 binutils 的反汇编库 opcodes, 感觉动辄上万行的代码对于 JOS 来说太过臃肿了, 而且缺乏文档也不知道该怎么用, 所以去网上找了个叫 [udis86](http://udis86.sourceforge.net) 的东西. 这个库的用法很简单, 而且本身有考虑过在内核中使用的情况, 所以使用的标准库非常少, 没有 malloc 之类的东西, 只需要改一下头文件就能在 JOS 上用. 因为反汇编只是个次要的附加功能, 所以为了让源代码目录整洁一点我把整个库塞进 `udis86.h` 和 `udis86.c` 两个文件里去了, 调用它的函数是 `kdebug.c` 中的 `step_inst()`. 由于没什么通用性这里就不贴了, 大致过程是把 trap frame 中 eip 附近的数据取出来传给反汇编库, 拿到结果后输出就可以了.

### Questions 2

 3. 权限设对就没问题了.
 
 4. 内核要正确地设置每种中断的权限?

### Exercise 7

`trapentry.S` 和 `trap_init()` 前面就写好了. `trap_dispatch()` 里面按 `eax`, `edx`, `ecx`, `ebx`, `edi`, `esi` 的参数顺序调 `syscall()`, 返回值存进 `eax`. 最后在 `kern/syscall.c` 的 `syscall()` 里面按照 `syscallno` 调相应的函数. 代码没什么意思.

### Challenge 3

这个 challenge 比较有意思, 首先要读文档了解 sysenter 的工作方式和 int 0x30 的区别, 然后开始写代码.

先在 `trap_init()` 里面初始化注册 handler

    wrmsr(0x174, GD_KT, 0);
    wrmsr(0x175, KSTACKTOP, 0);
    wrmsr(0x176, (uint32_t)sysenter_handler, 0);

那几个 magic number 是从手册里抄出来的. 这里比较坑的是题目给的链接404了, 得自己找.

然后按执行的先后顺序写吧, 最先执行的是 `lib/syscall.c` 中的系统调用, 把 `syscall(...)` 都改成 `sysenter(...)`, 写一个函数 `sysenter`:

    static inline int32_t sysenter(int num, int check,
            uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
    {
        int32_t ret;
    
        asm volatile(
                "pushl %%edx\n"
                "pushl %%ecx\n"
                "pushl %%ebx\n"
                "pushl %%edi\n"
                "pushl %%ebp\n"
                "movl %%esp, %%ebp\n"
                "leal 1f, %%esi\n"
                "sysenter\n"
                "1:\n"
                "popl %%ebp\n"
                "popl %%edi\n"
                "popl %%ebx\n"
                "popl %%ecx\n"
                "popl %%edx\n"
                : "=a" (ret)
                : "a" (num), "d" (a1), "c" (a2), "b" (a3), "D" (a4)
                : "cc", "memory");
    
        if (check && ret > 0) panic("sysenter %d returned %d (> 0)", num, ret);
        return ret;
    }

参数 `a5` 没有用到, 为了和 `syscall()` 格式保持一致还是加上了. 内联汇编中 `1f` 的意思是 `1:` 的地址, `f` 表示向下找, 相应地向上是 `b`. `"cc", "memory"` 那里我也不知道对不对, 照着 `syscall()` 抄的, 至少能过 grade.

`sysenter` 指令执行之后由处理器调用注册的 `sysenter_handler` (`trapentry.S`) :

    sysenter_handler:
        pushl $GD_UD | 3
        pushl %ebp
        pushfl
        pushl $GD_UT | 3
        pushl %esi
        pushl $0 // error code
        pushl $0 // trap number
    
        pushl %ds
        pushl %es
        pushal
    
        movw $GD_KD, %ax
        movw %ax, %ds
        movw %ax, %es
    
        pushl %esp
        call trap_sysenter
        pop %esp
    
        popal
        popl %es
        popl %ds
    
        movl %ebp, %ecx
        movl %esi, %edx
    
        sysexit

前面一半和 "An Example" 里面画的一样, 后面一半和 _alltraps 压的一样

`trap_sysenter()` 是一个和 `trap()` 的 `T_SYSCALL` 部分类似的函数:

    void trap_sysenter(struct Trapframe *tf)
    {
        curenv->env_tf = *tf;
        tf->tf_regs.reg_eax = syscall(
                tf->tf_regs.reg_eax,
                tf->tf_regs.reg_edx,
                tf->tf_regs.reg_ecx,
                tf->tf_regs.reg_ebx,
                tf->tf_regs.reg_edi,
                0);
    }

这里的 `syscall()` 是 `kern/syscall.c` 中的函数, 所以不需要改. (话说我一直觉得重名的文件和函数很讨厌)

这个 challenge 估计都是放到最后做的吧, `make grade` 检查一下就可以了.

### Exercise 8

就一行...

    thisenv = &envs[ENVX(sys_getenvid())];

### Exercise 9

内核态的 page fault 直接 panic:

    if (tf->tf_cs == GD_KT) panic("Kernel-mode Page Fault!\n");

然后实现 `user_mem_check()`

    if ((uint32_t)va >= ULIM) {
        user_mem_check_addr = (uint32_t)va;
        return -E_FAULT;
    }
    
    perm |= PTE_P;
    
    const void *va_end = ROUNDUP(va + len, PGSIZE);
    for (; va <= va_end; va = ROUNDUP(va + PGSIZE, PGSIZE)) {
        pte_t *pte = pgdir_walk(env->env_pgdir, va, 0);
        if (!pte || ((*pte & perm) != perm) {
            user_mem_check_addr = (uint32_t)va;
            return -E_FAULT;
        }
    }
    
    return 0;

这里比较坑的是 `user_mem_check_addr` 的问题, 本来权限是按页算的, 应该是同一个页内不管检查哪个地址都没有区别, 但是 grade 要求报错必须是指定的地址, 所以如果一开始把 va 对齐了就会过不了 hello, 如果后面循环的时候不对齐又会过不了 buggyhello2, 我在这个地方纠结了好久.

在 `sys_cputs()` 中检查地址

    user_mem_assert(curenv, s, len, 0);

在 `debuginfo_eip()` 中检查地址

    if (user_mem_check(curenv, usd, sizeof(struct UserStabData), PTE_U) < 0)
        return -1;
    
    /* ...... */
    
    if (user_mem_check(curenv, stabs, stab_end - stabs, PTE_U < 0) ||
            user_mem_check(curenv, stabstr, stabstr_end - stabstr, PTE_U) < 0)
        return -1;

### Exercise 10

这个 Exercise 是干嘛的 =_=

总结
====

做了个几个 Exercise 之后发现以前写的 buddy system 有一堆 bug, lab 2 用得太少了很多问题都没表现出来, 懒得写测试的后果... 另外做的时候也感觉到所有内存操作都要写两次实在有点蛋疼, 再加上之前赶时间的原因对 buddy system 的实现本来也不是很满意, 所以干脆就重构掉了. 现在 buddy system 的 API 已经和链表版本完全兼容, 以后如果要切换这两套系统只需要在 `kern/settings.h` 中修改相应的宏就可以了. 不过 buddy system 专用的 malloc/free API 还是没有改, 仍然是时间原因...

做 Challenge 1 的时候查了大量宏的用法, 学会了一堆奇技淫巧, 感觉某种意义上这玩意儿和 LaTeX 有一拼... 不过比起来还是更喜欢 c++ 的模版, 用来实现类似的功能宏的可读性差太多了.

做 Challenge 2 感受了一下 GNU 怪物级的库, 花了一个小时折腾 opcodes 最后还是没玩会. 感觉它设计的时候根本就只考虑了给熟悉这个项目的人用, 文档实在是太少了, 教程根本没有, 不熟悉的人想用也没地方下叉子. 最后的结论是 GNU 那些东西动态链接一下还行, 想整合进自己的项目还是洗洗睡吧.

最后看了一下 lab 4, 11 个 challenge...... \_(:з」∠)\_