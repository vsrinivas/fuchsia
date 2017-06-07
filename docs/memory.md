# Memory and resource usage

This file contains information about memory and resource management in Magenta,
and talks about ways to examine process and system memory usage.

*** note
**TODO**(dbort): Talk about the relationship between address spaces,
[VMARs](objects/vm_address_region.md), [mappings](syscalls/vmar_map.md), and
[VMOs](objects/vm_object.md)
***

[TOC]

## Userspace memory

Which processes are using all of the memory?

### Dump total process memory usage

Use the `ps` tool:

```
$ ps
TASK           PSS PRIVATE  SHARED NAME
j:1028       32.9M   32.8M         root
  p:1043   1386.3k   1384k     28k bin/devmgr
  j:1082     30.0M   30.0M         magenta-drivers
    p:1209  774.3k    772k     28k /boot/bin/acpisvc
    p:1565  250.3k    248k     28k devhost:root
    p:1619  654.3k    652k     28k devhost:misc
    p:1688  258.3k    256k     28k devhost:platform
    p:1867 3878.3k   3876k     28k devhost:pci#1:1234:1111
    p:1916   24.4M   24.4M     28k devhost:pci#3:8086:2922
  j:1103   1475.7k   1464k         magenta-services
    p:1104  298.3k    296k     28k crashlogger
    p:1290  242.3k    240k     28k netsvc
    p:2115  362.3k    360k     28k sh:console
    p:2334  266.3k    264k     28k sh:vc
    p:2441  306.3k    304k     28k /boot/bin/ps
TASK           PSS PRIVATE  SHARED NAME
```

**PSS** (proportional shared state) is a number of bytes that estimates how much
in-process mapped physical memory the process consumes. Its value is `PRIVATE +
(SHARED / sharing-ratio)`, where `sharing-ratio` is based on the number of
processes that share each of the pages in this process.

The intent is that, e.g., if four processes share a single page, 1/4 of the
bytes of that page is included in each of the four process's `PSS`. If two
processes share a different page, then each gets 1/2 of that page's bytes.

**PRIVATE** is the number of bytes that are mapped only by this process. I.e.,
no other process maps this memory. Note that this does not account for private
VMOs that are not mapped.

**SHARED** is the number of bytes that are mapped by this process and at least
one other process. Note that this does not account for shared VMOs that are not
mapped. It also does not indicate how many processes share the memory: it could
be 2, it could be 50.

### Visualize memory usage

If you have a Fuchsia build, you can use treemap to visualize memory usage by
the system.

 1. On your host machine, source the `env.sh` script into your shell.
 2. Also on your host machine, run the following command from the root of your
    Fuchsia checkout:

    ```fcmd ps --json | ./scripts/memory/treemap.py > mem.html```
 3. Open `mem.html` in a browser.

The `--json` flag instructs `ps` to produce output in JSON, which is then
parsed by the `treemap.py` script.

### Dump a process's detailed memory maps

If you want to see why a specific process uses so much memory, you can run the
`vmaps` tool on its koid (koid is the ID that shows up when running ps) and peer
into the tea leaves. (The memory ranges don't have good names yet, but if you
squint you can see dynamic libraries, heap, stack etc.)

```
$ vmaps help
Usage: vmaps <process-koid>

Dumps a process's memory maps to stdout.

First column:
  "/A" -- Process address space
  "/R" -- Root VMAR
  "R"  -- VMAR (R for Region)
  "M"  -- Mapping

  Indentation indicates parent/child relationship.
```

Column tags:

-   `:sz`: The virtual size of the entry, in bytes. Not all pages are
    necessarily backed by physical memory.
-   `:res`: The amount of memory "resident" in the entry, in bytes; i.e., the
    amount of physical memory that backs the entry. This memory may be private
    (only acceessable by this process) or shared by multiple processes.
-   `:vmo`: The `koid` of the VMO mapped into this region.

```
$ vmaps 2470
/A ________01000000-00007ffffffff000    128.0T:sz                    'unnamed'
/R ________01000000-00007ffffffff000    128.0T:sz                    'root'
...
# This 'R' region is a dynamic library. The r-x section is .text, the r--
# section is .rodata, and the rw- section is .data + .bss.
R  00000187bc867000-00000187bc881000      104k:sz                    'useralloc'
 M 00000187bc867000-00000187bc87d000 r-x   88k:sz   0B:res  2535:vmo 'libmxio.so'
 M 00000187bc87e000-00000187bc87f000 r--    4k:sz   4k:res  2537:vmo 'libmxio.so'
 M 00000187bc87f000-00000187bc881000 rw-    8k:sz   8k:res  2537:vmo 'libmxio.so'
...
# This 2MB anonymous mapping is probably part of the heap.
M  0000246812b91000-0000246812d91000 rw-    2M:sz  76k:res  2542:vmo 'mmap-anonymous'
...
# This region looks like a stack: a big chunk of virtual space (:sz) with a
# slightly-smaller mapping inside (accounting for a 4k guard page), and only a
# small amount actually committed (:res).
R  0000358923d92000-0000358923dd3000      260k:sz                    'useralloc'
 M 0000358923d93000-0000358923dd3000 rw-  256k:sz  16k:res  2538:vmo ''
...
# The stack for the initial thread, which is allocated differently.
M  0000400cbba84000-0000400cbbac4000 rw-  256k:sz   4k:res  2513:vmo 'initial-stack'
...
# The vDSO, which only has .text and .rodata.
R  000047e1ab874000-000047e1ab87b000       28k:sz                    'useralloc'
 M 000047e1ab874000-000047e1ab87a000 r--   24k:sz  24k:res  1031:vmo 'vdso/full'
 M 000047e1ab87a000-000047e1ab87b000 r-x    4k:sz   4k:res  1031:vmo 'vdso/full'
...
# The main binary for this process.
R  000059f5c7068000-000059f5c708d000      148k:sz                    'useralloc'
 M 000059f5c7068000-000059f5c7088000 r-x  128k:sz   0B:res  2476:vmo '/boot/bin/sh'
 M 000059f5c7089000-000059f5c708b000 r--    8k:sz   8k:res  2517:vmo '/boot/bin/sh'
 M 000059f5c708b000-000059f5c708d000 rw-    8k:sz   8k:res  2517:vmo '/boot/bin/sh'
...
```

### Dump all VMOs associated with a process

```
k mx vmos <pid>
```

This will also show unmapped VMOs, which neither `ps` nor `vmaps` currently
account for.

It also shows whether a given VMO is a clone, along with its parent's koid.

> NOTE: This is a kernel command, and will print to the kernel console.

```
$ k mx vmos 1102
process [1102]:
Handles to VMOs:
      handle rights  koid parent #chld #map #shr    size   alloc name
   158288097 rwxmdt  1144      -     0    1    1    256k      4k -
   151472261 r-xmdt  1031      -     0   22   11     28k     28k -
  total: 2 VMOs, size 284k, alloc 32k
Mapped VMOs:
           -      -  koid parent #chld #map #shr    size   alloc name
           -      -  1109   1038     1    1    1   25.6k      8k -
           -      -  1146   1109     0    2    1      8k      8k -
           -      -  1146   1109     0    2    1      8k      8k -
...
           -      -  1343      -     0    3    1    516k      8k -
           -      -  1325      -     0    1    1     28k      4k -
...
           -      -  1129   1038     1    1    1  883.2k     12k -
           -      -  1133   1129     0    1    1     16k     12k -
           -      -  1134      -     0    1    1     12k     12k -
           -      -  koid parent #chld #map #shr    size   alloc name
```

Columns:

-   `handle`: The `mx_handle_t` value of this process's handle to the VMO.
-   `rights`: The rights that the handle has, zero or more of:
    -   `r`: `MX_RIGHT_READ`
    -   `w`: `MX_RIGHT_WRITE`
    -   `x`: `MX_RIGHT_EXECUTE`
    -   `m`: `MX_RIGHT_MAP`
    -   `d`: `MX_RIGHT_DUPLICATE`
    -   `t`: `MX_RIGHT_TRANSFER`
-   `koid`: The koid of the VMO, if it has one. Zero otherwise. A VMO
    without a koid was created by the kernel, and has never had a userspace
    handle.
-   `parent`: The koid of the VMO's parent, if it's a clone.
-   `#chld`: The number of active clones (children) of the VMO.
-   `#map`: The number of times the VMO is currently mapped into VMARs.
-   `#shr`: The number of processes that map (share) the VMO.
-   `size`: The VMO's current size, in bytes.
-   `alloc`: The amount of physical memory allocated to the VMO, in bytes.
-   `name`: The name of the VMO, or `-` if its name is empty.

To relate this back to `ps`: each VMO contributes, for its mapped portions
(since not all or any of a VMO's pages may be mapped):

```
PRIVATE =  #shr == 1 ? alloc : 0
SHARED  =  #shr  > 1 ? alloc : 0
PSS     =  PRIVATE + (SHARED / #shr)
```

### Limitations

Neither `ps` nor `vmaps` currently account for:

-   VMOs or VMO subranges that are not mapped. E.g., you could create a VMO,
    write 1G of data into it, and it won't show up here.

None of the process-dumping tools account for:

-   Multiply-mapped pages. If you create multiple mappings using the same range
    of a VMO, any committed pages of the VMO will be counted as many times as
    those pages are mapped. This could be inside the same process, or could be
    between processes if those processes share a VMO.

    Note that "multiply-mapped pages" includes copy-on-write.
-   Underlying kernel memory overhead for resources allocated by a process.
    E.g., a process could have a million handles open, and those handles consume
    kernel memory.

    You can look at process handle consumption with the `k mx ps` command; run
    `k mx ps help` for a description of its columns.

## Kernel memory

*** note
**TODO**(dbort): Add commands/APIs to dump and examine kernel memory usage, and
document them here.
***
