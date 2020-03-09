# CPU Samples

The [Harvester](README.md) gathers a collection of CPU samples from each CPU on
the device. This document describes how the data is labelled and what the values
represent.

##### Dockyard Paths

The path to each sample will include "cpu", the processor index, and the sample
name: e.g. "cpu:0:busy_time".

### Samples

Data collected by the Harvester along with timestamp and a Dockyard Path is
called a Sample. The following sections describe CPU samples collected. They are
grouped in four broad categories.

#### Load

The busy and idle times are complementary. The busy time is derived from the
idle time.

##### cpu:count
The number of main CPUs. For the "cpu:\*:" sample paths the "\*" will be a
number from 0 to (cpu:count - 1).

##### cpu:\*:busy_time
The total accumulated time this processor has been busy.

##### cpu:\*:idle_time
The total accumulated time this processor was not doing work (not running any
threads).

#### Kernel scheduler counters

The scheduler schedules threads. It determines which thread a given CPU should
be running.

##### cpu:\*:reschedules
How many potential context_switches occurred. All context_switches are
preceded by a reschedule, but not all reschedules result in a context_switch.

##### cpu:\*:context_switches
How many thread switches have occurred. A high value may indicate threads
are thrashing (spending an inordinate amount of time switching places rather
than doing work).

##### cpu:\*:meaningful_irq_preempts
How many thread preemptions have occurred due to an interrupt (irq) while the
CPU is not idle. If the thread is idle the interrupt is not considered
meaningful and is not tracked here.

##### cpu:\*:preempts
How many thread preemptions have occurred. (Not currently used).

##### cpu:\*:yields
How many times a thread has yielded its use of a processor to allow another
thread to execute.

#### CPU level interrupts and exceptions

An interrupt causes the current flow of a thread to be "interrupted". The thread
is stopped, the state is preserved (so it can be resumed) and the processor
executes an interrupt handler (function).

##### cpu:\*:external_hardware_interrupts
External hardware interrupts indicate a signal or event happening outside of
the machine. Common examples are serial input, or a physical button like a
volume up or down button. Does not include timer or inter-processor interrupts.

##### cpu:\*:timer_interrupts
A timer interrupt occurs when a "clock" (or some kind of time keeping device)
creates an interrupt.

##### cpu:\*:timer_callbacks
How many times a function (callback) has been called due to a timer. Each time
a timer interrupt occurs, zero-to-many timer callbacks may be called.

##### cpu:\*:syscalls
How many system (kernel) API calls have been made.

#### Inter-processor interrupts

An inter-processor interrupt occurs when a processor signals another processor.

##### cpu:\*:reschedule_ipis
The count of times the scheduler (running on some CPU) has requested a
schedule change (waking up another CPU).

##### cpu:\*:generic_ipis
The count of inter-process interrupts that were not a schedule change (tracked
as reschedule_ipis), ipi interrupt (untracked), or ipi halt (untracked).

