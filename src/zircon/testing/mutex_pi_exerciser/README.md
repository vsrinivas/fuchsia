# uapp/mutex_pi_exerciser

A small test app which can be used to verify that priority inheritance behavior
has been implemented properly for various user mode synchronization objects.
This is not something which can be automatically tested, so kernel tracing and
manual verification needs to be done instead.

## Usage

After booting a target, run the application from a shell.  The application will
start, and then wait up to 5 seconds for the tracing framework to start.  Once
the application is waiting, a trace can be taken using

`fx traceutil record -duration 1s`

by default, the kernel:sched category should be enabled which should produce
trace events for priority inheritance interactions which produce a meaningful
change in effective priority.  Additional information can be captured in the
trace by enabling Futex tracing (kEnableFutexKTracing in
zircon/kernel/object/futex_context.cpp), and by turning up the tracing level for
kernel owned wait queues (setting kDefaultPiTracingLevel to
PiTracingLevel::Extended in zircon/kernel/kernel/owned_wait_queue.cpp).

## Tests and their expected behavior

Each test performed will put a process-wide trace event into the trace at the
start of the test which should indicate the type of the test, and the type of
object being tested.  Each test makes use of 5 threads at priority levels 3, 5,
7, 9, and 11.  The names of the threads should make it clear which is which, and
the threads are recycled for each test.  Three tests have been defined...

### The Mutex Chain test

This test forms a chain of mutexes and verifies that the mutexes properly
transmit priority through the chain.  Let T(n) be the thread with priority
thread, and M(n) be the mutex that this thread holds.  T(3) obtains M(3), and then
waits for the coordinator thread's signal.  Then T(5) obtains M(5) and attempts to
obtain M(3), which blocks because of T(3).  T(3), however, will receive the
pressure of M(5) causing its effective priority to rise to 5.  The process
repeats with T(7) obtaining M(7) and then attempting to obtain M(5).  This
causes T(5) to jump up to an EP of 7, and then transmit this priority to T(3)
whose EP should go from 5 -> 7.

This keeps going until we have T(11) -> T(9) -> T(7) -> T(5) -> T(3) and
everyone's effective priority is 11.  At this point, we start to unwind by
signalling T(3) to continue.  It drops M(3) and finishes the test.  T(5) enters
M(3) and then waits on its own signal.  We signal T(5) who drops M(3) and then
M(5), and finishes it's roll in the test.  At each stage, each thread should
drop down to its base priority after it releases its mutex (which is blocking
the next stage). Eventually, we have unwound everything and the test is over.

### The Multi-Waiter Mutex test

This test involves a single mutex (M) with multiple blocked threads.  The test
starts with T(3) obtaining M, and waiting for the coordinator's signal.  One at
a time, the other threads attempt to simply obtain and immediately release M.
Each time, they are blocked by T(3), and since they are joining in ascending
order of priority, each time T(3)'s effective priority jumps up until it finally
reaches priority 11.

At this point, the coordinator thread signals T(3) and the process of unwinding
starts. T(3)'s effective priority drops from 11 back down to 3 as it releases
the lock.  The schedule releases the waiting threads in order to decreasing
priority starting with T(11), and each thread flys through the lock.  No more
priority inheritance needs to happen at this point in time since the most
important waiter is being given the first shot at the lock.

### The CondVar broadcast test

This test involves a condition variable and it's associated lock type.  In order
of ascending priority, each
thread does the following...
1. Enters the mutex.
2. Checks to see if the test condition has been met.  In this case, the test
   condition is that their base priority is >= the exit threshold.  The
   threshold starts at an impossible-to-obtain value of 100.
3. Sleep for 250 uSec in order to force contention of the mutex.
4. If the test condition has not been met, the thread waits on the condition
   (which releases the lock in the process), then goes back to step 2.

When the test condition finally has been met, the thread will
1. Sets the exit threshold to be its own priority, minus two.
2. Broadcast signal the condition.
3. Finally, releases the mutex.

Once set up, the coordinator starts the process of unwinding by setting the exit
threshold to 13 and broadcast signalling the condition variable.

In each pass of the test, you see _all_ of the threads wake up and check the
condition, but only the highest priority thread is permitted to exit.  Once it
decides to exit, it lowers the threshold and re-signals the condition variable
causing all threads to wake up again.

It also should be noted that the only reason you see any priority inheritance
going on here at all is an artifact of the current cond_var implementations.
Current implementations go out of their wait to release threads from the
condition and transfer them over to own the mutex in the order that they blocked
on the mutex.  In addition, each waiting thread is blocking on its own futex
(instead of sharing a single futex).  As a result, when first signalled, T(3)
wakes up in the mutex and sleeps for a bit, receiving the priority pressure from
T(5) who is next in line, and finally waits on the condition again, waking T(5)
and transferring T(7) to the mutex's futex in the process.  So, each link in the
chain feel the priority on only the previous waiter in the chain, instead of all
the waiters.

A single futex could be used for the wait queue instead, however no meaningful
priority inheritance would be visible in a test structured like this.  When
signalled, one thread would be woken from the condition futex, while the rest of
the threads would be transferred to the mutex's futex wait queue.  The woken
thread would be T(11), so the pressure of the waiters would not effect it.  In
order to demonstrate PI in an implementation like this, a different low priority
thread would need to be lingering in the mutex at the time of the broadcast.
