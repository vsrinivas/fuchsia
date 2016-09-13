# Magenta System Calls

## Handles
+ [handle_close](syscalls/handle_close.md)
+ [handle_duplicate](syscalls/handle_duplicate.md)
+ [handle_replace](syscalls/handle_replace.md)
+ [handle_wait_many](syscalls/handle_wait_many.md)
+ [handle_wait_one](syscalls/handle_wait_one.md)

## Objects
+ [object_signal](syscalls/object_signal.md)

## Threads
+ [nanosleep](syscalls/nano_sleep.md)
+ [thread_arch_prctl](syscalls/thread_arch_prctl.md)
+ [thread_create](syscalls/thread_create.md)
+ [thread_exit](syscalls/thread_exit.md)
+ [thread_start](syscalls/thread_start.md)

## Processes
+ [process_create](syscalls/process_create.md)
+ [process_map_vm](syscalls/process_map_vm.md)
+ [process_protect_vm](syscalls/process_protect_vm.md)
+ [process_start](syscalls/process_start.md)
+ [process_unmap_vm](syscalls/process_unmap_vm.md)

## Message Pipes
+ [msgpipe_create](syscalls/msgpipe_create.md)
+ [msgpipe_read](syscalls/msgpipe_read.md)
+ [msgpipe_write](syscalls/msgpipe_write.md)

## Data Pipes
+ [datapipe_create](syscalls/datapipe_create.md)
+ [datapipe_write](syscalls/datapipe_write.md)
+ [datapipe_begin_write](syscalls/datapipe_begin_write.md)
+ [datapipe_end_write](syscalls/datapipe_end_write.md)
+ [datapipe_read](syscalls/datapipe_read.md)
+ [datapipe_begin_read](syscalls/datapipe_begin_read.md)
+ [datapipe_end_read](syscalls/datapipe_end_read.md)

## Wait Sets
+ [waitset_create](syscalls/waitset_create.md)
+ [waitset_add](syscalls/waitset_add.md)
+ [waitset_remove](syscalls/waitset_remove.md)
+ [waitset_wait](syscalls/waitset_wait.md)

## IO Ports
+ [port_create](syscalls/port_create.md)
+ [port_queue](syscalls/port_queue.md)
+ [port_wait](syscalls/port_wait.md)
+ [port_bind](syscalls/port_bind.md)

## Events and Event Pairs
+ [event_create](syscalls/event_create.md)
+ [eventpair_create](syscalls/eventpair_create.md)

## Futexes
+ [futex_wait](syscalls/futex_wait.md)
+ [futex_wake](syscalls/futex_wake.md)
+ [futex_requeue](syscalls/futex_requeue.md)

## Cryptographically Secure RNG
+ [cprng_draw](syscalls/cprng_draw.md)
+ [cprng_add_entropy](syscalls/cprng_add_entropy.md)
