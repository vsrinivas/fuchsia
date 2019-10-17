# [Harvester](README.md)

## Memory Samples

The Harvester gathers a collection of memory samples.

These counters are not always accurate. It’s possible for the sum of the various
page types (free_bytes, mmu_overhead_bytes, free_heap_bytes, etc.) to
temporarily add up to more than the total memory. In the rare cases where the
values don't add up accurately, the differences should only one or two memory
pages.

The counts are tracked by incrementing/decrementing atomic integers when a page
transitions from one state to another (e.g. from free_bytes to free_heap_bytes).
The relaxed accuracy allows for higher system performance. This can be adjusted
in the future, but so far there's been no call to make the trade for that extra
tiny bit of accuracy.

##### Dockyard Paths

The path to each sample will include "memory" and the sample name:
e.g. "memory:free_bytes".

### Samples

Graph data collected by the Harvester along with timestamp and a Dockyard Path
is called a Sample. The following sections describe the samples collected.

This data is often tracked in pages. So values will change by several KB at a
time.

#### Memory in the Device
Device memory refers to the memory within the machine. It's not specific to any
process or the kernel.

##### memory:device_total_bytes
The total physical memory available to the machine.

##### memory:device_free_bytes
The bytes within |device_total_bytes| that are unallocated.

#### Memory in the Kernel
This memory is related to the kernel rather than any user process or ipc.

##### memory:kernel_total_bytes
The total kernel bytes as reported by page state counter.

##### memory:kernel_free_bytes
The bytes within |kernel_total_bytes| that are unallocated.

##### memory:kernel_other_bytes
The amount of memory reserved by and mapped into the kernel for reasons not
reported elsewhere. Typically for read-only data like the RAM disk and kernel
image, and for early-boot dynamic memory.

#### Categorized Memory
These group memory used by category, with a catch-all 'other' category for
miscellaneous memory that doesn't fit in another category.

##### memory:vmo_bytes
The number of bytes used for Virtual Memory Objects. Ownership of a VMO may be
transferred between processes.

##### memory:mmu_overhead_bytes
Tracking the memory state also requires memory. This is the number of bytes of
overhead used for tracking page tables.

##### memory:ipc_bytes
Current amount of memory used for inter-process communication. Currently this
reflects the memory used for Zircon channels, but in the future it may include
memory for sockets and fifos.

##### memory:other_bytes
Memory that is in use but not tracked as kernel, user, ipc, etcetera.
