
# HotSort Gen

The ```hotsort_gen``` application generates HotSort compute kernel
source code for a particular platform and target.

A platform "target" is defined by a GPU vendor, architecture, and its
selected parameters.

The compute kernels are invoked by a platform-specific HotSort library.

A platform-specific HotSort library may require that the compute
kernel sources be further transformed and packaged.
