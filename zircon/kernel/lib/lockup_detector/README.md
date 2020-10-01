# lib/lockup_detector -- library for detecting kernel lockups

This library provides instrumentation for detecting when the kernel
has remained in a critical section for "too long".  Critical sections
are marked using `LOCKUP_BEGIN()` and `LOCKUP_END()`.  The code
executing under a spinlock is an example of a critical section.  A
function that temporarily disables interrupts would be another
example.

## Self Checking

Currently, detection is performed only when a CPU leaves a critical
section (calls `LOCKUP_END()`).  If a CPU calls `LOCKUP_BEGIN()`, but
never calls `LOCKUP_END()`, the instrumentation will not detect the
"lockup".  However, there is a `k` command that can be used to check
if a CPU is currently in a critical section: `k lockup status`.  This
command is intended to be used as an interactive diagnostic tool.

## Threshold

Choosing an appropriate lockup threshold is important.  The value
should be small enough to detect performance impacting lockups, but
large enough to avoid false alarms, especially when running on
virtualized hardware.

See also `kernel.lockup-detector.threshold-ms`.

## Future Directions

A future version of this library may add instrumentation for detecting
when a CPU has been "executing kernel code for too long", or has been
in a hypervisor or Secure Monitor call for too long.  A future version
may also integrate with a software watchdog to take corrective action
when lockups are detected.
