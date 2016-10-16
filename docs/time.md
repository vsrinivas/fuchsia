# Time units

## Userspace exposed time units
mx\_time\_t is nanoseconds

## Kernel-internal time units
lk\_time\_t is milliseconds
lk\_bigtime\_t is nanoseconds

The kernel-internal time units are likely to change but mx\_time\_t is expected to be stable.
