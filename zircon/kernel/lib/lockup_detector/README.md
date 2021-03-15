# lib/lockup_detector -- library for detecting kernel lockups

This library provides instrumentation for detecting different kinds of
lockups.

The library has two tools, a critical section detector and a CPU
heartbeat checker.

## Critical Section Detector

The critical section detector detects when the kernel has remained in
a critical section for "too long".  Critical sections are marked using
`LOCKUP_TIMED_BEGIN()` and `LOCKUP_TIMED_END()`.  The code executing
during an SMC call is an example of a critical section.  A function
that temporarily disables interrupts would be another example.

### Self Checking and Cross Checking

Currently, detection is performed in two ways.

First, each time a CPU leaves a critical section (calls
`LOCKUP_TIMED_END()`), it will observe the time spent in the critical
section, and then update kcounters which track the number of long
running critical sections we have seen.  Additionally, it will track
the "worst case" critical section time for that CPU.

Second, If a CPU calls `LOCKUP_TIMED_BEGIN()`, but never calls
`LOCKUP_TIMED_END()`, and the CPU has spent more than the configured
threshold amount of time in the critical section, a lockup will be
reported as a `KERNEL_OOPS` by another one of the CPUs when it is
performing a heartbeat check.  If the amount of time spent by the
locked-up CPU in the critical section exceeds the fatal threshold, the
kernel will generate a crashlog and reboot, indicating a reboot reason
of `SOFTWARE_WATCHDOG` in the crashlog as it does.

The `k lockup status` command can be used to check if a CPU is
currently in a critical section, and to print the current worst case
critical section times for each CPU.

### Threshold

Choosing appropriate thresholds for the critical section detector is
important.  The values should be small enough to detect performance
impacting lockups, but large enough to avoid false alarms, especially
when running on virtualized hardware.  Setting both of these values to
0 will completely disable critical section lockup detection.

See also `kernel.lockup-detector.critical-section-threshold-ms` and
`kernel.lockup-detector.critical-section-fatal-threshold-ms`.

## Heartbeak Checker

The heartbeat checker is used to detect when a CPU has stopped
responding to interrupts.

When enabled, all CPUs will run a periodic timer callback that emits a
"heartbeat" by updating a timestamp in its per CPU structure.
Afterwards, they will check each of the other CPUs' last heartbeat and
if the last heartbeat is older than the configured threshold, a (rate
limited) `KERNEL_OOPS` will be emitted.  If the time since last
heartbeat exceeds the fatal threshold, a crashlog will be generated
and the kernel will reboot, indicating a reboot reason of
`SOFTWARE_WATCHDOG` in the crashlog as it does.

`kernel.lockup-detector.heartbeat-period-ms` controls how frequently
the CPUs emit heartbeats and perform checks.

`kernel.lockup-detector.heartbeat-age-threshold-ms` controls how long
a CPU can go without emitting a heartbeat before it is considered to
be locked up and a `KERNEL_OOPS` is generated.

`kernel.lockup-detector.heartbeat-age-fatal-threshold-ms` controls how
long a CPU can go without emitting a heartbeat before a
`SOFTWARE_WATCHDOG` reboot is triggered.

## Future Directions

A future version of this library may add instrumentation for detecting
when a CPU has been "executing kernel code for too long", or has been
in a hypervisor or Secure Monitor call for too long.
