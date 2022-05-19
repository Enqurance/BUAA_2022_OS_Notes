```C
/*** exercise 4.2 ***/
NESTED(handle_sys,TF_SIZE, sp)
    SAVE_ALL                            // Macro used to save trapframe
    CLI                                 // Clean Interrupt Mask
    nop
    .set at                             // Resume use of $at

    // TODO: Fetch EPC from Trapframe, calculate a proper value and store it back to trapframe.
        lw t0, TF_EPC(sp)
        addiu t0, t0, 4
        sw t0, TF_EPC(sp)

    // TODO: Copy the syscall number into $a0.
    lw a0, TF_REG4(sp)

    addiu   a0, a0, -__SYSCALL_BASE     // a0 <- relative syscall number
    sll     t0, a0, 2                   // t0 <- relative syscall number times 4
    la      t1, sys_call_table          // t1 <- syscall table base
    addu    t1, t1, t0                  // t1 <- table entry of specific syscall
    lw      t2, 0(t1)                   // t2 <- function entry of specific syscall

    lw      t0, TF_REG29(sp)            // t0 <- user's stack pointer

        /* t2存储了异常处理函数的地址，t0存储了用户栈指针，sp存储了内核栈指针 */

        // 1. store the first 4 arg into reg, not include cnt, the 5th arg may in stack if exists
  			/* 使用宏，将前四个参数存入寄存器，其中TF_REG5对应的是参数数目 */
        lw      a0, TF_REG4(sp)
        lw      a1, TF_REG6(sp)
        lw      a2, TF_REG7(sp)
				/* 将参数数目存入s1，s1位cnt */
        lw      s1, TF_REG5(sp) // s1 = cnt
        /* 向t3写入3，准备比较 */
        li              t3, 3
  			/* t0为用户栈指针位置，加上16后为第五个参数的位置 */
        addiu   t0, t0, 16 // user_sp += 16, at stack arg field
				/* 如果参数个数小于3，跳转 */
        blt     s1, t3, STORE_a3_END
        nop
        lw              a3, 0(t0)
        addiu   t0, t0, 4 // user_sp += 4, at stack arg field
STORE_a3_END:

        li              t3, 4 // t3 = 4, cnt >= 4, stack has args

        // 3. adjust sys_sp place
        addiu   t4, s1, 1 // t4 = cnt + 1
        sll             s2, t4, 2 // s2 = (cnt + 1) * 4

        // just as mips promise, all args num is cnt + 1, save such **s2** places for them
        // but here is a bug, args num least is 4, cnt may less then 3
        // u may need to fix this, threat
        subu    sp, sp, s2 // sys_sp down, save place for stack arg
        addiu   t5, sp, 16 // where put stack args, t5


// now, copy user_sp args into sys_sp stack field
COPY_ARG_BEG:
        bgt             t3, s1, COPY_ARG_END // t3 = 4, begin
        nop
        lw              t4, 0(t0)
        addiu   t0, t0, 4
        sw              t4, 0(t5)
        addiu   t5, t5, 4
        addiu   t3, t3, 1
        j               COPY_ARG_BEG
        nop
COPY_ARG_END:

        jalr    t2
        nop

        // back, return saved args space
        addu sp, sp, s2

    sw      v0, TF_REG2(sp)             // Store return value of function sys_* (in $v0) into trapframe

    j       ret_from_exception          // Return from exeception
    nop
END(handle_sys)

sys_call_table:                         // Syscall Table
.align 2
    .word sys_putchar
    .word sys_getenvid
    .word sys_yield
    .word sys_env_destroy
    .word sys_set_pgfault_handler
    .word sys_mem_alloc
    .word sys_mem_map
    .word sys_mem_unmap
    .word sys_env_alloc
    .word sys_set_env_status
    .word sys_set_trapframe
    .word sys_panic
    .word sys_ipc_can_send
    .word sys_ipc_recv
    .word sys_cgetc
    .word sys_ipc_can_multi_send
```

