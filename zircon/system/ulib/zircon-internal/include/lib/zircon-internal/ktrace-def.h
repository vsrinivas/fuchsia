// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KTRACE_DEF
#error must define KTRACE_DEF
#endif

// events before 0x100 may require specialized handling

KTRACE_DEF(0x000, 32B, VERSION, META)       // version
KTRACE_DEF(0x001, 32B, TICKS_PER_MS, META)  // lo32, hi32

KTRACE_DEF(0x020, NAME, KTHREAD_NAME, META)    // ktid, 0, name[]
KTRACE_DEF(0x021, NAME, THREAD_NAME, META)     // tid, pid, name[]
KTRACE_DEF(0x022, NAME, PROC_NAME, META)       // pid, 0, name[]
KTRACE_DEF(0x023, NAME, SYSCALL_NAME, META)    // num, 0, name[]
KTRACE_DEF(0x024, NAME, IRQ_NAME, META)        // num, 0, name[]
KTRACE_DEF(0x025, NAME, PROBE_NAME, META)      // num, 0, name[]
KTRACE_DEF(0x026, NAME, VCPU_META, META)       // meta, 0, name[]
KTRACE_DEF(0x027, NAME, VCPU_EXIT_META, META)  // meta, 0, name[]

KTRACE_DEF(0x030, 16B, IRQ_ENTER, IRQ)          // (irqn << 8) | cpu
KTRACE_DEF(0x031, 16B, IRQ_EXIT, IRQ)           // (irqn << 8) | cpu
KTRACE_DEF(0x032, 16B, SYSCALL_ENTER, SYSCALL)  // (n << 8) | cpu
KTRACE_DEF(0x033, 16B, SYSCALL_EXIT, SYSCALL)   // (n << 8) | cpu

KTRACE_DEF(0x034, 32B, PAGE_FAULT, VM)       // virtual_address_hi, virtual_address_lo, flags, cpu
KTRACE_DEF(0x035, 32B, PAGE_FAULT_EXIT, VM)  // virtual_address_hi, virtual_address_lo, flags, cpu

// to-tid, ((new_thread_prioriy<<24) | (old_thread_priority<<16) | (old_thread_state<<8) | cpu),
// from-kt, to-kt
KTRACE_DEF(0x040, 32B, CONTEXT_SWITCH, SCHEDULER)

// Word 0: inherit event ID
// Word 1: 0
// Word 2: 0
// Word 3: bits [0, 7] CPU ID
KTRACE_DEF(0x041, 32B, INHERIT_PRIORITY_START, SCHEDULER)

// Word 0: inherit event ID.
// Word 1: Target thread TID.
// Word 2: bits [ 0,  7] : old effective priority as an int8_t
//       : bits [ 8, 15] : new effective priority as an int8_t
//       : bits [16, 23] : old inherited priority as an int8_t
//       : bits [24, 31] : new inherited priority as an int8_t
// Word 3: bits [ 0,  7] : CPU ID
//       : bit 8         : Kernel TID flag.
//       : bit 9         : Final event flag.
//
// Note: Target thread TID is one of two things.  Either, the lower 32
// bits of the KOID for a user mode thread dispatcher object, or the
// lower 32 bits of a kernel mode thread structure's kernel address.
// The Kernel TID flag in word #3 can be used to tell the difference
// (1 => kernel TID, 0 => user mode TID)
//
KTRACE_DEF(0x042, 32B, INHERIT_PRIORITY, SCHEDULER)

// Word 0: futex ID bits [0..31]
// Word 1: futex ID bits [32..63]
// Word 2: new owner user-mode TID, or 0 for no owner
// Word 3: bits [0..7] CPU ID
KTRACE_DEF(0x043, 32B, FUTEX_WAIT, SCHEDULER)

// Word 0: futex ID bits [0..31]
// Word 1: futex ID bits [32..63]
// Word 2: zx_status_t result of the wait operation.
// Word 3: bits [0..7] CPU ID
KTRACE_DEF(0x044, 32B, FUTEX_WOKE, SCHEDULER)

// Word 0: futex ID bits [0..31]
// Word 1: futex ID bits [32..63]
// Word 2: assigned owner user-mode TID, or 0 for no owner
// Word 3: bits [0..7] CPU ID
// Word 3: bits [8..15] Low 8 bits of count
//                      [0, 0xFE) => count
//                      0xFE      => count was >= 0xFE
//                      0xFF      => count was unlimited
// Word 3: bit 30; 1 => requeue operation, 0 => wake operation
// Word 3: bit 31; 1 => futex was active, 0 => it was not.
KTRACE_DEF(0x045, 32B, FUTEX_WAKE, SCHEDULER)

// Word 0: requeue target futex ID bits [0..31]
// Word 1: requeue target futex ID bits [32..63]
// Word 2: assigned owner user-mode TID, or 0 for no owner
// Word 3: bits [0..7] CPU ID
// Word 3: bits [8..15] Low 8 bits of count
//                      [0, 0xFE) => count
//                      0xFE      => count was >= 0xFE
//                      0xFF      => count was unlimited
// Word 3: bit 31; 1 => futex was active, 0 => it was not.
KTRACE_DEF(0x046, 32B, FUTEX_REQUEUE, SCHEDULER)

// Trace events logged when a thread interacts with a kernel mutex.  Three
// operations are defined, Acquire, release and block, but the meaning of the
// parameters vary slightly from operation to operation.
//
// Acquire operations are only logged in the case that a mutex is obtained while
// uncontested.  There will never be a TID specified, and the number of waiters
// will always be 0.
//
// Release operations will only log a TID in the case that the mutex is being
// granted atomically to a specific waiter, and the TID will specify the waiter
// that the mutex is being release to.  In the case that an uncontested mutex is
// being released, the TID will be 0.
//
// Block operations will log a TID indicating the TID of the thread which
// current owns the mutex and is blocking them.
//
// Word 0: low 32 bits of kernel mutex address
// Word 1: TID
// Word 2: The number of threads blocked on this mutex
// Word 3: bits [0..7] CPU ID
// Word 3: bit 31: 1 => word 1 TID is user mode
//                 0 => word 1 TID is kernel mode
KTRACE_DEF(0x047, 32B, KERNEL_MUTEX_ACQUIRE, SCHEDULER)
KTRACE_DEF(0x048, 32B, KERNEL_MUTEX_RELEASE, SCHEDULER)
KTRACE_DEF(0x049, 32B, KERNEL_MUTEX_BLOCK, SCHEDULER)

KTRACE_DEF(0x100, 32B, OBJECT_DELETE, LIFECYCLE)  // id

KTRACE_DEF(0x110, 32B, THREAD_CREATE, TASKS)  // tid, pid
KTRACE_DEF(0x111, 32B, THREAD_START, TASKS)   // tid
KTRACE_DEF(0x112, 32B, THREAD_EXIT, TASKS)

KTRACE_DEF(0x120, 32B, PROC_CREATE, TASKS)  // pid
KTRACE_DEF(0x121, 32B, PROC_START, TASKS)   // tid, pid
KTRACE_DEF(0x122, 32B, PROC_EXIT, TASKS)    // pid

KTRACE_DEF(0x130, 32B, CHANNEL_CREATE, IPC)  // id0, id1, flags
KTRACE_DEF(0x131, 32B, CHANNEL_WRITE, IPC)   // id0, bytes, handles
KTRACE_DEF(0x132, 32B, CHANNEL_READ, IPC)    // id1, bytes, handles

KTRACE_DEF(0x140, 32B, PORT_WAIT, IPC)       // id
KTRACE_DEF(0x141, 32B, PORT_WAIT_DONE, IPC)  // id, status
KTRACE_DEF(0x142, 32B, PORT_CREATE, IPC)     // id
KTRACE_DEF(0x143, 32B, PORT_QUEUE, IPC)      // id, size

KTRACE_DEF(0x150, 32B, WAIT_ONE, IPC)       // id, signals, timeoutlo, timeouthi
KTRACE_DEF(0x151, 32B, WAIT_ONE_DONE, IPC)  // id, status, pending

KTRACE_DEF(0x160, 32B, KWAIT_BLOCK, SCHEDULER)    // queue_hi, queue_hi
KTRACE_DEF(0x161, 32B, KWAIT_WAKE, SCHEDULER)     // queue_hi, queue_hi, is_mutex
KTRACE_DEF(0x162, 32B, KWAIT_UNBLOCK, SCHEDULER)  // queue_hi, queue_hi, blocked_status

KTRACE_DEF(0x170, 32B, VCPU_ENTER, TASKS)
KTRACE_DEF(0x171, 32B, VCPU_EXIT, TASKS)     // meta, exit_address_hi, exit_address_lo
KTRACE_DEF(0x172, 32B, VCPU_BLOCK, TASKS)    // meta
KTRACE_DEF(0x173, 32B, VCPU_UNBLOCK, TASKS)  // meta

// events from 0x200-0x2ff are for arch-specific needs

#ifdef __x86_64__
// These are used by Intel Processor Trace support.
KTRACE_DEF(0x200, 32B, IPT_START, ARCH)     // MSR_PLATFORM_INFO[15:8], kernel cr3
KTRACE_DEF(0x201, 32B, IPT_CPU_INFO, ARCH)  // family, model, stepping
KTRACE_DEF(0x202, 32B, IPT_STOP, ARCH)
KTRACE_DEF(0x203, 32B, IPT_PROCESS_CREATE, ARCH)  // pid, cr3
#endif

#undef KTRACE_DEF
