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
magenta$ ps
TASK        VIRT     RES NAME
p:1041     16.2M   1716k bin/devmgr
j:1068                   magenta-drivers
  p:1208   13.1M   1676k devhost:root
  p:1473  261.7M   1624k /boot/bin/acpisvc
  p:1621   4216k   1260k devhost:acpi
  p:1669   4172k   1240k devhost:pci#0:8086:29c0
  p:1756   30.1M   4684k devhost:pci#1:1234:1111
  p:1818   4172k   1240k devhost:pci#2:8086:2918
  p:1993   6724k   1760k devhost:pci#3:8086:2922
  p:2180   4172k   1240k devhost:pci#4:8086:2930
j:1082                   magenta-services
  p:1083   4716k   1332k crashlogger
  p:1173   4032k   1176k netsvc
  p:2988   4220k   1324k sh:console
  p:3020   4136k   1296k sh:vc
  p:3183   4044k   1108k /boot/bin/ps
TASK        VIRT     RES NAME
```

**RES** (resident memory) is the one to care about: it shows how many physical
pages are mapped into the process, and physical pages are what the system runs
out of.

**VIRT** is the amount of potentially-mappable virtual address space allocated
to a process (via mappings on a VMAR). Apart from the metadata necessary to keep
track of the ranges, high VIRT values don't necessarily mean high physical
memory usage.

### Dump a process's detailed memory maps

If you want to see why a specific process is using so much memory, you can run
the `vmaps` tool on its koid (koid is the ID that shows up when running ps) and
peer into the tea leaves. (The memory ranges don't have good names yet, but if
you squint you can see dynamic libraries, heap, stack etc.)

```
magenta$ vmaps help
Usage: vmaps <process-koid>

Dumps a process's memory maps to stdout.

First column:
  "/A" -- Process address space
  "/R" -- Root VMAR
  "R"  -- VMAR (R for Region)
  "M"  -- Mapping

  Indentation indicates parent/child relationship.
```

In the `vmaps` output, **:sz** (size) is the same as `ps`'s **VIRT**, and
**:res** is the same as `ps`'s **RES**.

```
magenta$ vmaps 3020
/A ________01000000-00007ffffffff000      128.0T:sz              unnamed
/R ________01000000-00007ffffffff000      128.0T:sz              root
...
# These two 'R' regions are probably loaded ELF files like the main binary or
# dynamic objects, since they contain a fully-committed r-x mapping with a mix
# of rw- and r-- mappings.
R  ________01000000-________01025000        148k:sz              useralloc
 M ________01000000-________01021000 r-x    132k:sz    132k:res  useralloc
 M ________01021000-________01023000 r--      8k:sz      8k:res  useralloc
 M ________01023000-________01024000 rw-      4k:sz      4k:res  useralloc
 M ________01024000-________01025000 rw-      4k:sz      4k:res  useralloc
R  ________01025000-________0102e000         36k:sz              useralloc
 M ________01025000-________0102c000 r-x     28k:sz     28k:res  useralloc
 M ________0102c000-________0102d000 r--      4k:sz      4k:res  useralloc
 M ________0102d000-________0102e000 rw-      4k:sz      4k:res  useralloc
...
# These two regions look like stacks: a big chunk of virtual space with a
# slightly-smaller mapping inside (accounting for a 4k guard page), and only a
# small amount actually committed (RES).
R  ________01048000-________01089000        260k:sz              useralloc
 M ________01049000-________01089000 rw-    256k:sz     16k:res  useralloc
R  ________01089000-________010ca000        260k:sz              useralloc
 M ________0108a000-________010ca000 rw-    256k:sz      0B:res  useralloc
...
# This collection of rw- mappings could include the heap (possibly the VIRT=2M
# mapping), along with other arbitrary VMO mappings.
M  ________010ca000-________012ca000 rw-      2M:sz     32k:res  useralloc
M  ________012ca000-________012d1000 rw-     28k:sz      4k:res  useralloc
M  ________012d1000-________012d9000 rw-     32k:sz     20k:res  useralloc
M  ________012d9000-________012da000 rw-      4k:sz      4k:res  useralloc
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
magenta$ k mx vmos 1102
[00005.269] 01041.01044> process [1102]:
[00005.269] 01041.01044> Handles to VMOs:
[00005.269] 01041.01044>       handle rights  koid #map parent #chld    size   alloc name
[00005.299] 01041.01044>   1686510157 rwxmdt  1144    1      -     0    256k      4k -
[00005.300] 01041.01044>   1692801539 r-xmdt  1031   22      -     0     32k     32k -
[00005.300] 01041.01044>   total: 2 VMOs, size 288k, alloc 36k
[00005.300] 01041.01044> Mapped VMOs:
[00005.300] 01041.01044>            -      -  koid #map parent #chld    size   alloc name
[00005.301] 01041.01044>            -      -  1166    1   1038     1   29.7k      8k -
[00005.301] 01041.01044>            -      -  1168    2   1166     0      8k      8k -
[00005.301] 01041.01044>            -      -  1168    2   1166     0      8k      8k -
[00005.301] 01041.01044>            -      -  1211    3      -     0    516k     16k -
[00005.301] 01041.01044>            -      -  1270    1      -     0      4k      4k -
...
[00005.302] 01041.01044>            -      -  1129    1   1038     1  883.2k     12k -
[00005.302] 01041.01044>            -      -  1133    1   1129     0     16k     12k -
[00005.302] 01041.01044>            -      -  1134    1      -     0     12k     12k -
[00005.302] 01041.01044>            -      -  koid #map parent #chld    size   alloc name
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
-   `#map`: The number of times the VMO is currently mapped into VMARs.
-   `parent`: The koid of the VMO's parent, if it's a clone.
-   `#chld`: The number of active clones (children) of the VMO.
-   `size`: The VMO's current size, in bytes.
-   `alloc`: The amount of physical memory allocated to the VMO, in bytes.
-   `name`: The name of the VMO, or `-` if its name is empty.

### Limitations

Neither `ps` nor `vmaps` currently account for:

-   VMOs or VMO subranges that are not mapped. E.g., you could create a VMO,
    write 1G of data into it, and it won't show up here.

None of the process-dumping tools account for:

-   Multiply-mapped pages. If you create multiple mappings using the same range
    of a VMO, any committed pages of the VMO will be counted (in RES) as many
    times as those pages are mapped. This could be inside the same process, or
    could be between processes if those processes share a VMO.

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
