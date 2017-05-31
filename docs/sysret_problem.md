# Avoiding a problem with the SYSRET instruction

On x86-64, the kernel uses the SYSRET instruction to return from system
calls.  We must be careful not to use a non-canonical return address with
SYSRET, at least on Intel CPUs, because this causes the SYSRET instruction
to fault in kernel mode, which is potentially unsafe.  (In contrast, on AMD
CPUs, SYSRET faults in user mode when used with a non-canonical return
address.)

Usually, the lowest non-negative non-canonical address is 0x0000800000000000
(== 1 << 47).  One way that a user process could cause the syscall return
address to be non-canonical is by mapping a 4k executable page immediately
below that address (at 0x00007ffffffff000), putting a SYSCALL instruction
at the end of that page, and executing the SYSCALL instruction.

To avoid this problem:

* We disallow mapping a page when the virtual address of the following page
  will be non-canonical.

* We disallow setting the RIP register to a non-canonical address using
  **mx_thread_write_state**() when the address would be used with SYSRET.

For more background, see "A Stitch In Time Saves Nine: A Case Of Multiple
OS Vulnerability", Rafal Wojtczuk
(https://media.blackhat.com/bh-us-12/Briefings/Wojtczuk/BH_US_12_Wojtczuk_A_Stitch_In_Time_WP.pdf).
