# `early_boot_instrumentation`

Reviewed on: 2022-01-10

`early_boot_instrumentation` exists to collect and expose all coverage information from
processes that exist before or outside the component framework. Such processes consist
and are not limited to:
    - Physboot
    - Kernel
    - Boot Service


## Building

To add this project to your build, append `--with
//src/sys/early_boot_coverage` to the `fx set` invocation.

## Running

`early_boot_instrumentation` groups the profile data in two groups, a static and a dynamic one.
Static profile data refers to data that is not being updated while dynamic profile data
is constantly changing. This is the result of processes who have exited versus those that
are continuously running. For example a boot loader might expose static profile data, while
the kernel exposes dynamic profile data.

Data is exposed in the form of VMO Files in two directories offered to this component's parent,
'prof-data/static' and 'prof-data/dynamic'.
