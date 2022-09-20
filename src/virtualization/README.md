# Fuchsia Virtualization

This directory contains the userspace portions of the Fuchsia Virtualization
stack. For a high level overview of Fuchsia Virtualization, see [Virtualization
Overview][ref.virtualization_overview].

If you just want to start using Virtualization, see [Getting
Started][ref.virtualization_get_started].

# Monitor Guest exit statistics

`kstats -v`  can print the number of Guest exits per second and the reason. For
example, the output on ARM64 appears as follows:

```none
cpu   vm_entry vm_exit inst_abt data_abt wfx_inst sys_inst smc_inst ints
  0      43        43        0      6      27        1        0      9
  1     226       225        0    111       3       17        0     94
  2     109       109        0     60       8        7        0     35
  3      58        58        0     21      12        2        0     23
```

The fields in the output are as follows:
- `inst_abt`: The amount of instruction abort exit.
- `data_abt`: The amount of data abort exit.
- `wfx_inst`: The amount of instruction wfe/wfi exit.
- `sys_inst`: The amount of systen register access exit.
- `smc_inst`: The amount of instruction smc exit.
- `ints`    : The amount of interrupt exit.

For `x64`, the output is as follows:

```none
 cpu    vm_entry vm_exit ints ints_win ept ctrl_reg msr(rd wr) inst(io hlt cpuid ple vmcall xsetbv)
  0       40       40     10      0     0      0        10 10        5  5    0    0    0      0
```

With the following fields:
- `ints`     : The amount of interrupt exit.
- `ints_win` : The amount of interrupt window exit.
- `ept`      : The amount of EPT violation exit.
- `ctrl_reg` : The amount of control register(CRx) access exit.
- `msr`      : The amount of MSR register access exit (`rd/wr` is read/write).
- `inst`     : the amount of some kinds of instruction exit.

[ref.virtualization_overview]:
    https://fuchsia.dev/fuchsia-src/development/virtualization/overview
[ref.virtualization_get_started]:
    https://fuchsia.dev/fuchsia-src/development/virtualization/get_started