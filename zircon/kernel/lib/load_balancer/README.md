# Periodic processor load balancer

This is a thread that wakes periodically to examine the distribution of load
on the system and move threads between processors in an attempt to provide
greater balance and improve system utilization. (This functionality is planned
but not yet implemented)

It is also responsible for generating data the scheduler will use when a
thread unblocks for cpu selection. Currently it will generate a threshold at
which the cpu should start looking through an ordered list of cpus that it
should consider sending newly awakened threads to. It will write these to the
load_balancer::CpuState object attached to each percpu struct.

## Current factors
The following factors will be in the current implementation. They focus on
mostly existing data and leverage the global view of the system this provides to
make suggestions.

### Current queue time on cpus

We will look at a recent average of the queue time on each cpu as an indicator
of its load. We will use the times in aggregate to determine the threshold under
which a cpu shouldn't even bother trying to load-shed, since the whole system is
loaded to this level.

## Planned factors

The following factors are planned for implementation soon. They depend on
interpreting thread interactions as a graph.

### Projected load of threads in wait queues but assigned to a cpu

We will track which cpu all threads in wait queues are assigned and determine an
aggregate predicted load that blocked thread could provide if they wake during
this cycle. We will apply a decay to blocked threads and the longer they are
asleep the less their load will factor into the aggregate load.

### Cross cache communication

We will track the threads a thread communicates with by volume of data sent.
Using this we will try to keep those communications sharing as many levels of
cache as possible to optimize that communication, we see up to 300ns of overhead
for a cacheline between L2 caches.

### Reschedule IPIs

We will track the threads a thread communicates with the most often. Using this
data we will try to keep threads the communicate a lot on the same CPU as there
is real cost (800-1000ns) to waking a thread on a different CPU and sending it
an IPI to reschedule.
