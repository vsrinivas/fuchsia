# Intel Processor Trace driver

See Chapter 36 of the Intel Architecture Software Developer's Manual.

## Trace modes

There are two modes of tracing:

- per cpu
- specified threads (not implemented yet)

Only one may be active at a time.

### Per CPU tracing

In this mode of operation each cpu is traced, regardless of what is
running on the cpu, except as can be controlled by PT configuration MSRs
(e.g., cr3 filtering, kernel/user, address filtering).

### Specified thread tracing

In this mode of operation individual threads are traced, even as they
migrate from CPU to CPU. This is achieved via the PT state save/restore
capabilities of the XSAVES and XRSTORS instructions.

Filtering control (e.g., cr3, user/kernel) is not available in this mode.
Address filtering is possible, but is still TODO.

## FIDL

The interface for driving insntruction tracing is specified by
`//sdk/fidl/fuchsia.hardware.cpu.insntrace/insntrace.fidl`.

## Usage

Here's a sketch of typical usage when tracing in cpu mode.

1) *Initialize(IPT_MODE_CPUS, num_cpus)*
2) allocate buffers for each cpu
3) *Start()*
4) launch program one wishes to trace
5) *Stop()*
6) fetch buffer data for each cpu
7) fetch handles for each vmo in each buffer, and save data
8) *Terminate()* [this will free each buffer as well]
9) post-process

## Notes

- We currently only support Table of Physical Addresses mode so that
we can also support stop-on-full behavior in addition to wrap-around.

- Each cpu/thread has the same size trace buffer.

- While it's possible to allocate and configure buffers outside of the driver,
this is not done so that we have control over their contents. ToPA buffers
must have specific contents or Bad Things can happen.

## TODOs (beyond those in the source)

- support tracing individual threads using xsaves

- handle driver crashes
  - need to turn off tracing
  - need to keep buffer/table vmos alive until tracing is off
