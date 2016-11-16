# Magenta System Calls

## Handles
+ [handle_close](syscalls/handle_close.md) - close a handle
+ [handle_duplicate](syscalls/handle_duplicate.md) - create a duplicate handle (optionally with reduced rights)
+ [handle_replace](syscalls/handle_replace.md) - create a new handle (optionally with reduced rights) and destroy the old one
+ [handle_wait_many](syscalls/handle_wait_many.md) - wait for signals on multiple handles
+ [handle_wait_one](syscalls/handle_wait_one.md) - wait for signals on one handle

## Objects
+ object_bind_exception_port - attach an exception port to a task
+ object_get_child - find the child of an object by its koid
+ object_get_info - obtain information about an object
+ object_get_property - read an object property
+ object_set_property - modify an object property
+ [object_signal](syscalls/object_signal.md) - set or clear the user signals on an object
+ [object_signal_peer](syscalls/object_signal_peer.md) - set or clear the user signals in the opposite end

## Threads
+ [thread_arch_prctl](syscalls/thread_arch_prctl.md) - deprecated
+ [thread_create](syscalls/thread_create.md) - create a new thread within a process
+ [thread_exit](syscalls/thread_exit.md) - exit the current thread
+ thread_read_state - read register state from a thread
+ [thread_start](syscalls/thread_start.md) - cause a new thread to start executing
+ thread_write_state - modify register state of a thread

## Processes
+ [process_create](syscalls/process_create.md) - create a new process within a job
+ [process_map_vm](syscalls/process_map_vm.md) - map a VMO into a process
+ [process_protect_vm](syscalls/process_protect_vm.md) - adjust memory access permissions
+ process_read_memory - read from a process's address space
+ [process_start](syscalls/process_start.md) - cause a new process to start executing
+ [process_unmap_vm](syscalls/process_unmap_vm.md) - unmap a memory region from a process
+ process_write_memory - write to a process's address space

## Jobs
+ job_create - create a new job within a job

## Tasks (Task, Process, or Job)
+ task_resume - cause a suspended task to continue running
+ task_kill - cause a task to stop running

## Channels
+ [channel_create](syscalls/channel_create.md) - create a new channel
+ [channel_read](syscalls/channel_read.md) - receive a message from a channel
+ [channel_write](syscalls/channel_write.md) - write a message to a channel

## Sockets
+ [socket_create](syscalls/socket_create.md) - create a new socket
+ [socket_write](syscalls/socket_write.md) - write data to a socket
+ [socket_read](syscalls/socket_read.md) - read data from a socket

## Events and Event Pairs
+ [event_create](syscalls/event_create.md) - create an event
+ [eventpair_create](syscalls/eventpair_create.md) - create a connected pair of events

## Wait Sets
+ [waitset_create](syscalls/waitset_create.md) - create a new waitset
+ [waitset_add](syscalls/waitset_add.md) - add an entry to a waitset
+ [waitset_remove](syscalls/waitset_remove.md) - remove an entry from a waitset
+ [waitset_wait](syscalls/waitset_wait.md) - wait for one or more entries to be signalled

## Ports
+ [port_create](syscalls/port_create.md) - create a port
+ [port_queue](syscalls/port_queue.md) - send a packet to a port
+ [port_wait](syscalls/port_wait.md) - wait for packets to arrive on a port
+ [port_bind](syscalls/port_bind.md) - bind an object to a port

## Futexes
+ [futex_wait](syscalls/futex_wait.md)
+ [futex_wake](syscalls/futex_wake.md)
+ [futex_requeue](syscalls/futex_requeue.md)

## Virtual Memory Objects (VMOs)
+ [vmo_create](syscalls/vmo_create.md) - create a new vmo
+ [vmo_read](syscalls/vmo_read.md) - read from a vmo
+ [vmo_write](syscalls/vmo_write.md) - write to a vmo
+ [vmo_get_size](syscalls/vmo_get_size.md) - obtain the size of a vmo
+ [vmo_set_size](syscalls/vmo_set_size.md) - adjust the size of a vmo
+ [vmo_op_range](syscalls/vmo_op_range.md) - perform an operation on a range of a vmo

## Cryptographically Secure RNG
+ [cprng_draw](syscalls/cprng_draw.md)
+ [cprng_add_entropy](syscalls/cprng_add_entropy.md)

## Time
+ [nanosleep](syscalls/nano_sleep.md) - sleep for some number of nanoseconds
+ [time_get](syscalls/time_get.md) - read a system clock

## Data Pipes
+ [datapipe_create](syscalls/datapipe_create.md)
+ [datapipe_write](syscalls/datapipe_write.md)
+ [datapipe_begin_write](syscalls/datapipe_begin_write.md)
+ [datapipe_end_write](syscalls/datapipe_end_write.md)
+ [datapipe_read](syscalls/datapipe_read.md)
+ [datapipe_begin_read](syscalls/datapipe_begin_read.md)
+ [datapipe_end_read](syscalls/datapipe_end_read.md)

## Information
+ version_get - get kernel version string
+ num_cpus - get number of cores

## Logging
+ log_create - create a kernel managed log reader or writer
+ log_write - write log entry to log
+ log_read - read log entries from log
