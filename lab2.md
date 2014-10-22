Part 1: Physical Page Management
================================

### Exercise 1

  * `boot_alloc()` 没什么难的, 就是 `boot_alloc(0)` 用来查"堆顶"有点坑;

  * `mem_init()` 暂时没什么意思;

  * `page_init()` 的关键是 `EXTPHYSMEM` 之后有多少已使用的内存;

  * `page_alloc()` 提示我们去看看那些在各种有关地址的类型之间来回转的宏;

  * `page_free()` 很简单.

比较奇怪的是如果不执行 `check_page_free_list(1)` (确切地说是其中交换高低地址顺序的部分) 就
会崩溃, 即使所有 exercises 全部做完还是这样, 不知道是我写的有问题还是设计本来如此. 看了一下教
学网上的报告感觉和我做的差不多, 可能也会这样.

Part 2: Virtual Memory
======================

### Exercise 2

看就是了...

Virtual, Linear, and Physical Addresses
---------------------------------------

### Excercise 3

qemu 的控制台在我的电脑上用快捷键打不开, 解决方法是在 makefile 的 `QEMUOPTS` 里加上
`-monitor telnet:127.0.0.1:6828,server,nowait` , 然后就可以用
`telnet 127.0.0.1 6828` 来看控制台了.

`info pg` 对第二个 challenge 有点帮助, 可以编译一下 patch 过的 qemu. 我顺便更新了 AUR 上
的 `qemu-mit6828`, 如果有人用 Arch 可以去装一下...

### Question

 1. `uintptr_t`, 因为指针都是虚拟地址.

Reference counting
------------------

Page Table Management
---------------------

### Excercise 4

  * `pgdir_walk()` 照着注释写就行...

  * `boot_map_region()` 还是照着注释写...

  * `page_lookup()` 仍然照着注释写...

  * `page_remove()` ......

  * `page_insert()` 终于有东西说了! 注释表示有一种优雅的方式可以不用判断插入的地址和以前是否
    相同, 我想了好久才写出来. 贴代码:

        pte_t *pte = pgdir_walk(pgdir, va, 1);
        if (!pte) return -E_NO_NUM;
        
        pp->pp_ref++;
        if (*pte & PTE_P)
            page_remove(pgdir, va);
        
        *pte = page2pa(pp) | perm | PTE_P;
        return 0;

    好像贴出来也没什么要解释的了... 大概就是地址相同的时候如果直接用 `page_remove()` 可能会
    错误地 free 掉, 那么只需要先增加引用计数就可以了, 这样即使减一次之后也不会变成0.

Part 3: Kernel Address Space
============================

Permissions and Fault Isolation
-------------------------------

Initializing the Kernel Address Space
-------------------------------------

### Excercise 5

就3行... KERNBASE 到最高地址的长度可以利用补码写成 `-KERNBASE`, 不过也没啥意思.

### Question

 2. | Entry | Base Virtual Address |              Points to              |
    |:-----:|:--------------------:|:-----------------------------------:|
    | 1023  |      0xffc00000      |Page table for top 4MB of phys memory|
    | 1022  |      0xff800000      |Page table for 248~252MB of phys mem |
    |   .   |         ...          |                 ...                 |
    |  960  |      0xf0000000      |       Page table for KERNBASE       |
    |  959  |      0xefc00000      |       Page table for MMIOLIM        |
    |  958  |      0xef800000      |         Page table for ULIM         |
    |  957  |      0xef400000      |         Page table for UVPT         |
    |  956  |      0xef000000      |        Page table for UPAGES        |
    |   .   |         ...          |                 ...                 |
    |   2   |      0x00800000      |                empty                |
    |   1   |      0x00400000      |                empty                |
    |   0   |      0x00000000      | Page table for KERNBASE during boot |

 3. 页表项中的 PTE_U 决定了用户的权限.
 
 4. 256MB, 从 KERN_BASE 到 0xffffffff 只有这么多地址.
 
 5. 最多 65536 个页, 故需 512KB 维护 PageInfo, 256KB 维护页表(含页目录), 共 768KB
 
 6. `mov $relocated, %eax ; jmp *%eax` 从低地址跳转到高地址. 由于 entrypgdir.c 把
    虚拟地址的 0~4M 也映射到了物理地址的 0~4M, 所以刚启动时可以在低地址运行.

### Challenge 1

首先在 `entry.S` 中开启 `cr4` 的 `CR4_PSE` 位, 这个步骤不会对不支持 PSE 的 CPU 产生影响.

然后在 `i386_detect_memory()` 中使用 `cpuid` 检查 CPU 是否支持 PSE (具体来说是检查
`%edx` 的 bit 3), 如果支持则设置一个标记, 可以开始使用大页.

需要修改的函数有:

  * `pgdir_walk()` 对4M页直接返回页目录, 并且不进行内存分配;
  * `boot_map_region()` 分配4M页时直接查页目录不使用 `pgdir_walk()`;
  * `check_va2pa()` 对于4M页可以返回参数中虚拟地址对应的物理地址来通过检查;

最后在对 `KERNBASE` 进行 `boot_map_region()` 时加上 `PTE_PS` 权限位就可以了.

如果同时使用自映射和PSE, 页目录中的4M表项也会出现在二级页表中, 不知道这样会不会产生比较严重的
问题, 暂时还没有处理.

### Challenge 2

`showmappings()` 可以参考 qemu [`monitor.c`][1] 中的 `pg_info()`, 需要注意一下如果使用
32位整型对太高的地址做加法会溢出变成很小的数, 所以循环写起来有点恶心.

设置页面权限和 `showmappings()` 差不多, 打印改成修改就可以了.

dump 内存时如果使用了未映射的虚拟地址会导致崩溃, 由于设计上这只是个调试用的函数所以我没有处理
这种细节.

总之这个 Challenge 难度不高但代码有点长, 比较民工...

Address Space Layout Alternatives
---------------------------------

### Challenge 3

没做

### Challenge 4

首先查阅伙伴系统的教程, 我参考的是 [coolshell][2] 上的一篇文章, 文中给出的实现相当简洁, 讲解
也很清楚, 我就不再赘述了. 唯一需要修改的地方是原文末尾提到的直接使用32位整型记录可用空间非常浪
费, 事实上可用空间的大小要么是0要么是2的整数次方, 所以可以用对数来记录. 这样只需要使用5位就可
以支持约2^(2⁵-1)个页, 远超32位地址空间的需求了, 剩下3位还可用于引用计数, 从而每个页只需要用2
个字节就可以支持最多2047的引用计数, 4个字节则可以支持约50万引用计数. (可惜要求支持 256MB 内
存, 如果只需要支持 128MB 可以再省一位)

需要修改的函数主要有:

  * `page_init()` 改为初始化伙伴系统, 为了方便预留不可用空间这里可以从叶节点向上初始化;
  * `page_alloc()` 重写成 `kmalloc()`;
  * `page_free()` 重写成 `kfree()`;
  * `pgdir_walk()` 改为使用 `kmalloc()` 分配空间;
  * `page_decref()`, `page_insert()`, `page_lookup()`, `page_remove()` 这几个使用 
    `PageInfo` 的函数改为直接使用 `physaddr_t`.

最后在 `mem_init()` 中使用伙伴系统版本的函数即可.

另外需要重写 `check_*()` 函数, 我基本是按照原样写的, 但跳过了一些诸如检查空闲链表之类的操作.

目前暂未实现对 user-level environment 的 superpage 支持, 因为现在连用户环境都没有就算写了
也根本没法调...


[1]: https://github.com/geofft/qemu/commit/387498d70dd3e13c9aebb74ddf43fc5d9bd67ffe
[2]: http://coolshell.cn/articles/10427.html