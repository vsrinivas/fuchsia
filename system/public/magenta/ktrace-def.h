// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KTRACE_DEF
#error must define KTRACE_DEF
#endif

// events before 0x100 may require specialized handling

KTRACE_DEF(0x000,32B,VERSION,META) // version
KTRACE_DEF(0x001,32B,TICKS_PER_MS,META) // lo32, hi32

KTRACE_DEF(0x020,NAME,KTHREAD_NAME,META) // ktid, 0, name[]
KTRACE_DEF(0x021,NAME,THREAD_NAME,META) // tid, pid, name[]
KTRACE_DEF(0x022,NAME,PROC_NAME,META) // pid, 0, name[]
KTRACE_DEF(0x023,NAME,SYSCALL_NAME,META) // num, 0, name[]
KTRACE_DEF(0x024,NAME,IRQ_NAME,META) // num, 0, name[]
KTRACE_DEF(0x025,NAME,PROBE_NAME,META) // num, 0, name[]

KTRACE_DEF(0x030,16B,IRQ_ENTER,IRQ) // (irqn << 8) | cpu
KTRACE_DEF(0x031,16B,IRQ_EXIT,IRQ) // (irqn << 8) | cpu
KTRACE_DEF(0x032,16B,SYSCALL_ENTER,IRQ) // (n << 8) | cpu
KTRACE_DEF(0x033,16B,SYSCALL_EXIT,IRQ) // (n << 8) | cpu

KTRACE_DEF(0x034,32B,PAGE_FAULT,IRQ) // virtual_address_hi, virtual_address_lo, flags, cpu

KTRACE_DEF(0x040,32B,CONTEXT_SWITCH,SCHEDULER) // to-tid, (state<<16|cpu), from-kt, to-kt

// events from 0x100 on all share the tag/tid/ts common header

KTRACE_DEF(0x100,32B,OBJECT_DELETE,LIFECYCLE) // id

KTRACE_DEF(0x110,32B,THREAD_CREATE,TASKS) // tid, pid
KTRACE_DEF(0x111,32B,THREAD_START,TASKS) // tid
KTRACE_DEF(0x112,32B,THREAD_EXIT,TASKS)

KTRACE_DEF(0x120,32B,PROC_CREATE,TASKS) // pid
KTRACE_DEF(0x121,32B,PROC_START,TASKS) // tid, pid
KTRACE_DEF(0x122,32B,PROC_EXIT,TASKS) // pid

KTRACE_DEF(0x130,32B,CHANNEL_CREATE,IPC) // id0, id1, flags
KTRACE_DEF(0x131,32B,CHANNEL_WRITE,IPC) // id0, bytes, handles
KTRACE_DEF(0x132,32B,CHANNEL_READ,IPC) // id1, bytes, handles

KTRACE_DEF(0x140,32B,PORT_WAIT,IPC) // id
KTRACE_DEF(0x141,32B,PORT_WAIT_DONE,IPC) // id, status
KTRACE_DEF(0x142,32B,PORT_CREATE,IPC) // id
KTRACE_DEF(0x143,32B,PORT_QUEUE,IPC) // id, size

KTRACE_DEF(0x150,32B,WAIT_ONE,IPC) // id, signals, timeoutlo, timeouthi
KTRACE_DEF(0x151,32B,WAIT_ONE_DONE,IPC) // id, status, pending

// events from 0x200-0x2ff are for arch-specific needs

#ifdef __x86_64__
KTRACE_DEF(0x200,32B,IPT_START,ARCH) // MSR_PLATFORM_INFO[15:8], kernel cr3
KTRACE_DEF(0x201,32B,IPT_CPU_INFO,ARCH) // family, model, stepping
KTRACE_DEF(0x202,32B,IPT_STOP,ARCH)
KTRACE_DEF(0x203,32B,IPT_PROCESS_CREATE,ARCH) // pid, cr3
#endif

#undef KTRACE_DEF
