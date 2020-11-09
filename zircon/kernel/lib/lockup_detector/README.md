# lib/lockup_detector -- library for detecting kernel lockups

This library provides instrumentation for detecting different kinds of
lockups.

The library has two tools, a critical section detector and a CPU
heartbeat checker.

## Critical Section Detector

The critical section detector detects when the kernel has remained in
a critical section for "too long".  Critical sections are marked using
`LOCKUP_BEGIN()` and `LOCKUP_END()`.  The code executing under a
spinlock is an example of a critical section.  A function that
temporarily disables interrupts would be another example.

### Self Checking

Currently, detection is performed only when a CPU leaves a critical
section (calls `LOCKUP_END()`).  If a CPU calls `LOCKUP_BEGIN()`, but
never calls `LOCKUP_END()`, the instrumentation will not detect the
"lockup".  However, there is a `k` command that can be used to check
if a CPU is currently in a critical section: `k lockup status`.  This

### Threshold

Choosing an appropriate threshold for the critical section detector is
important.  The value should be small enough to detect performance
impacting lockups, but large enough to avoid false alarms, especially
when running on virtualized hardware.

See also `kernel.lockup-detector.critical-section-threshold-ms`.

## Heartbeak Checker

The heartbeat checker is used to detect when a CPU has stopped
responding to interrupts.

Each secondary CPU will run a periodic timer callback that emits a
"heartbeat" by updating a timestamp in its per CPU structure.  The
primary CPU will peroidically check each secondary CPU's last
heartbeat and if the last heartbeat is older than the configured
threshold, a (rate limited) KERNEL OOPS will be emitted.

`kernel.lockup-detector.heartbeat-period-ms` controls how frequently
secondary CPUs emit heartbeats.

`kernel.lockup-detector.heartbeat-age-threshold-ms` controls how long
a CPU can go without emitting a heartbeat before it is considered to
be locked up.

## Future Directions

A future version of this library may add instrumentation for detecting
when a CPU has been "executing kernel code for too long", or has been
in a hypervisor or Secure Monitor call for too long.  A future version
may also integrate with a software watchdog to take corrective action
when lockups are detected.
