# Magenta System Calls

## Handles
+ [handle_close](syscalls/handle_close.md) - close a handle
+ [handle_duplicate](syscalls/handle_duplicate.md) - create a duplicate handle (optionally with reduced rights)
+ [handle_replace](syscalls/handle_replace.md) - create a new handle (optionally with reduced rights) and destroy the old one

## Objects
+ [object_get_child](syscalls/object_get_child.md) - find the child of an object by its koid
+ [object_get_cookie](syscalls/object_get_cookie.md) - read an object cookie
+ [object_get_info](syscalls/object_get_info.md) - obtain information about an object
+ [object_get_property](syscalls/object_get_property.md) - read an object property
+ [object_set_cookie](syscalls/object_set_cookie.md) - write an object cookie
+ [object_set_property](syscalls/object_set_property.md) - modify an object property
+ [object_signal](syscalls/object_signal.md) - set or clear the user signals on an object
+ [object_signal_peer](syscalls/object_signal.md) - set or clear the user signals in the opposite end
+ [object_wait_many](syscalls/object_wait_many.md) - wait for signals on multiple objects
+ [object_wait_one](syscalls/object_wait_one.md) - wait for signals on one object
+ [object_wait_async](syscalls/object_wait_async.md) - asynchronous notifications on signal change

## Threads
+ [thread_create](syscalls/thread_create.md) - create a new thread within a process
+ [thread_exit](syscalls/thread_exit.md) - exit the current thread
+ [thread_read_state](syscalls/thread_read_state.md) - read register state from a thread
+ [thread_start](syscalls/thread_start.md) - cause a new thread to start executing
+ [thread_write_state](syscalls/thread_write_state.md) - modify register state of a thread

## Processes
+ [process_create](syscalls/process_create.md) - create a new process within a job
+ [process_read_memory](syscalls/process_read_memory.md) - read from a process's address space
+ [process_start](syscalls/process_start.md) - cause a new process to start executing
+ [process_write_memory](syscalls/process_write_memory.md) - write to a process's address space
+ [process_exit](syscalls/process_exit.md) - exit the current process

## Jobs
+ [job_create](syscalls/job_create.md) - create a new job within a job
+ [job_set_policy](syscalls/job_set_policy.md) - modify policies for a job and its descendants
+ [job_set_relative_importance](syscalls/job_set_relative_importance.md) - update a global ordering of jobs

## Tasks (Thread, Process, or Job)
+ [task_resume](syscalls/task_resume.md) - cause a suspended task to continue running
+ [task_bind_exception_port](syscalls/task_bind_exception_port.md) - attach an exception port to a task
+ [task_kill](syscalls/task_kill.md) - cause a task to stop running

## Channels
+ [channel_call](syscalls/channel_call.md) - synchronously send a message and receive a reply
+ [channel_create](syscalls/channel_create.md) - create a new channel
+ [channel_read](syscalls/channel_read.md) - receive a message from a channel
+ [channel_write](syscalls/channel_write.md) - write a message to a channel

## Sockets
+ [socket_create](syscalls/socket_create.md) - create a new socket
+ [socket_read](syscalls/socket_read.md) - read data from a socket
+ [socket_write](syscalls/socket_write.md) - write data to a socket

## Fifos
+ [fifo_create](syscalls/fifo_create.md) - create a new fifo
+ [fifo_read](syscalls/fifo_read.md) - read data from a fifo
+ [fifo_write](syscalls/fifo_write.md) - write data to a fifo

## Events and Event Pairs
+ [event_create](syscalls/event_create.md) - create an event
+ [eventpair_create](syscalls/eventpair_create.md) - create a connected pair of events

## Ports
+ [port_create](syscalls/port_create.md) - create a port
+ [port_queue](syscalls/port_queue.md) - send a packet to a port
+ [port_wait](syscalls/port_wait.md) - wait for packets to arrive on a port
+ [port_cancel](syscalls/port_cancel.md) - cancel notificaitons from async_wait

## Futexes
+ [futex_wait](syscalls/futex_wait.md) - wait on a futex
+ [futex_wake](syscalls/futex_wake.md) - wake waiters on a futex
+ [futex_requeue](syscalls/futex_requeue.md) - wake some waiters and requeue other waiters

## Virtual Memory Objects (VMOs)
+ [vmo_create](syscalls/vmo_create.md) - create a new vmo
+ [vmo_read](syscalls/vmo_read.md) - read from a vmo
+ [vmo_write](syscalls/vmo_write.md) - write to a vmo
+ [vmo_clone](syscalls/vmo_clone.md) - clone a vmo
+ [vmo_get_size](syscalls/vmo_get_size.md) - obtain the size of a vmo
+ [vmo_set_size](syscalls/vmo_set_size.md) - adjust the size of a vmo
+ [vmo_op_range](syscalls/vmo_op_range.md) - perform an operation on a range of a vmo

## Virtual Memory Address Regions (VMARs)
+ [vmar_allocate](syscalls/vmar_allocate.md) - create a new child VMAR
+ [vmar_map](syscalls/vmar_map.md) - map a VMO into a process
+ [vmar_unmap](syscalls/vmar_unmap.md) - unmap a memory region from a process
+ [vmar_protect](syscalls/vmar_protect.md) - adjust memory access permissions
+ [vmar_destroy](syscalls/vmar_destroy.md) - destroy a VMAR and all of its children

## Cryptographically Secure RNG
+ [cprng_draw](syscalls/cprng_draw.md)
+ [cprng_add_entropy](syscalls/cprng_add_entropy.md)

## Time
+ [nanosleep](syscalls/nanosleep.md) - sleep for some number of nanoseconds
+ [time_get](syscalls/time_get.md) - read a system clock
+ [ticks_get](syscalls/ticks_get.md) - read high-precision timer ticks
+ [ticks_per_second](syscalls/ticks_per_second.md) - read the number of high-precision timer ticks in a second

## Timers
+ [timer_create](syscalls/timer_create.md) - create a timer object
+ [timer_set](syscalls/timer_set.md) - start a timer
+ [timer_cancel](syscalls/timer_cancel.md) - cancel a timer

## Global system information
+ [system_get_num_cpus](syscalls/system_get_num_cpus.md) - get number of CPUs
+ [system_get_physmem](syscalls/system_get_physmem.md) - get physical memory size
+ [system_get_version](syscalls/system_get_version.md) - get version string

## Logging
+ log_create - create a kernel managed log reader or writer
+ log_write - write log entry to log
+ log_read - read log entries from log

## Multi-function
+ [vmar_unmap_handle_close_thread_exit](syscalls/vmar_unmap_handle_close_thread_exit.md) - three-in-one
+ [futex_wake_handle_close_thread_exit](syscalls/futex_wake_handle_close_thread_exit.md) - three-in-one
