# Task Samples

The [Harvester](README.md) gathers Task samples from jobs, processes, and
threads on the Fuchsia device. This document describes how the data is labelled
and what the values represent.

##### Dockyard Paths

The path to each sample will include "koid", the kernel object ID (koid), and
the sample name: e.g. "koid:12345:zircon-services".

### Samples

Data collected by the Harvester along with timestamp and a Dockyard Path is
called a Sample. The following sections describe Task samples collected. They
are presented in three groups, some values (e.g. name) appear in multiple
groups.

#### Job

A job has permissions. A job will have one or more processes (which have memory
mappings), and each process will have one or more thread (which execute on a
CPU).

##### koid:\*:type
This will always be `dockyard::KoidType::JOB` for a job.

##### koid:\*:parent_koid
The koid of the object that started this job.

##### koid:\*:name
A UTF-8 string label for the job. May not be unique.

##### koid:\*:kill_on_oom
If the Fuchsia device is low on memory (i.e. oom or Out Of Memory) this job (and
its child processes and their threads) may be 'killed' (i.e. terminated) for the
good of the rest of the system. E.g. the job is not critical. A Boolean value,
1 is true (will be killed) and 0 is false.

#### Process

A process has memory. In contrast, a thread accesses memory owned by the
Process, but threads themselves don't have their own (memory) address range.

A byte of memory is considered committed if it's backed by physical memory.
Some of the memory may be double-mapped, and thus double-counted. Samples where
this may occur are marked "May be counted by more than one mapping" below.

##### koid:\*:type
This will always be `dockyard::KoidType::PROCESS` for a process.

##### koid:\*:parent_koid
The koid of the object that started this process (e.g. its job).

##### koid:\*:name
A UTF-8 string label for the job. May not be unique.

##### koid:\*:memory_mapped_bytes
The total size of mapped memory ranges in the process, though not all will be
backed by physical memory.

##### koid:\*:memory_private_bytes
Committed memory that is only mapped into this process. May be counted by more
than one mapping.

##### koid:\*:memory_shared_bytes
Committed memory that is mapped into this and at least one other process. May be
counted by more than one mapping.

##### koid:\*:memory_scaled_shared_bytes
An estimate of the prorated number of mem_shared_bytes used by this process.

Calculated by:
    For each shared, committed byte,
        memory_scaled_shared_bytes += 1 / (number of process mapping this byte)

The memory_scaled_shared_bytes will be smaller than memory_shared_bytes. May be
counted by more than one mapping.

See zx_info_task_stats_t in zircon/system/public/zircon/syscalls/object.h for up
to date information.

#### Thread

A thread exists within a process and each process will have at least one thread.
Threads actually execute (use the CPU) while a process does not.

A thread does not have its own memory address space. Instead threads use the
memory address space of their parent process.

##### koid:\*:type
This will always be `dockyard::KoidType::THREAD` for a thread.

##### koid:\*:parent_koid
The koid of the object that started this thread (e.g. its process).

##### koid:\*:name
A UTF-8 string label for the job. May not be unique.

##### koid:\*:thread_state
Whether the thread is running, waiting, etc.
The current (when this was written) thread states are:

```
Basic thread states, in zx_info_thread_t.state.
    ZX_THREAD_STATE_NEW                 0x0000
    ZX_THREAD_STATE_RUNNING             0x0001
    ZX_THREAD_STATE_SUSPENDED           0x0002
    // ZX_THREAD_STATE_BLOCKED is never returned by itself.
    // It is always returned with a more precise reason.
    // See ZX_THREAD_STATE_BLOCKED_* below.
    ZX_THREAD_STATE_BLOCKED             0x0003
    ZX_THREAD_STATE_DYING               0x0004
    ZX_THREAD_STATE_DEAD                0x0005

More precise thread states.
    ZX_THREAD_STATE_BLOCKED_EXCEPTION   0x0103
    ZX_THREAD_STATE_BLOCKED_SLEEPING    0x0203
    ZX_THREAD_STATE_BLOCKED_FUTEX       0x0303
    ZX_THREAD_STATE_BLOCKED_PORT        0x0403
    ZX_THREAD_STATE_BLOCKED_CHANNEL     0x0503
    ZX_THREAD_STATE_BLOCKED_WAIT_ONE    0x0603
    ZX_THREAD_STATE_BLOCKED_WAIT_MANY   0x0703
    ZX_THREAD_STATE_BLOCKED_INTERRUPT   0x0803
    ZX_THREAD_STATE_BLOCKED_PAGER       0x0903
```

See zircon/system/public/zircon/syscalls/object.h for an up to date reference.
