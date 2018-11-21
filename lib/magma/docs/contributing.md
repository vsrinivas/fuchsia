## Magma Contributing and Best Practices

### Submitting a patch

See [Contributing](../../../CONTRIBUTING.md).

### Source Code

The source code for a magma graphics driver may be hosted entirely within the garnet repository.

The core magma code is found under:

* [lib/magma/src](../src)

Implementations of the magma service drivers are found under:

* [drivers/gpu](../../../drivers/gpu)

Implementations of the magma application driver may be located in drivers/gpu; though
often these are built from third party projects, such as third_party/mesa.

### Coding Conventions and Formatting

* Use the **[Google style guide](https://google.github.io/styleguide/cppguide.html)** for source code (except 4 spaces for indent).
* Run **clang-format** on your changes to maintain consistent formatting.

### Testing Pre-Submit

For details on the testing strategy for magma, see [test_strategy.md](test_strategy.md).

There are multiple levels for magma TPS.  Each level includes all previous levels.

When submitting a change you must indicate the TPS level tested, preface by the hardware
on which the testing was performed:

TEST:  
nuc,vim2:go/magma-tps#L2  
nuc,vim2:go/magma-tps#S1  
nuc,vim2:go/magma-tps#C0  
nuc,vim2:go/magma-tps#P0  

#### L0

Includes all unit tests and integration tests.  There are 2 steps at this tps level:

1. Build with --arg magma_enable_developer_mode=true; this will run unit tests that require hardware,
then present the device as usual for general applications.  Inspect the syslog for test results.

2. Run the test script [lib/magma/scripts/test.sh](../../../lib/magma/scripts/test.sh) and inspect the test results.

#### L1

If you have an attached display, execute the spinning [vkcube](../../../lib/vulkan/tests/vkcube).
This test uses an imagepipe swapchain to pass frames to the system compositor.  
Build with --packages garnet/packages/examples/vkcube_on_scenic.  
Run the test with 'set_root_view vkcube_on_scenic'.

#### L2

A full UI 'smoke' test. Build the entire product including your change.  
Login, start a few apps, move them around, and dismiss them.
For details, refer to top level project documentation.

#### S0

For stress testing, run the test script [lib/magma/scripts/stress.sh](../../../lib/magma/scripts/stress.sh)
and ensure that the driver does not leak resources over time.

#### S1

A full UI stress test.  Launch the spinning_cube example and the infinite_scroller, and let them run overnight.

#### C0

For some changes, it's appropriate to run the vulkan conformance test suite before submitting.
See [Conformance](#conformance).

#### P0

For some changes, it's appropriate to run benchmarks to validate performance metrics. See [Benchmarking](#benchmarking).

### Conformance ###

For details on the Vulkan conformance test suite, see

* [../third_party/vulkan-cts](../../../../third_party/vulkan-cts)

### Benchmarking ###

The source to Vulkan gfxbench is access-restricted. It should be cloned into third_party.

* https://fuchsia-vendor-internal.git.corp.google.com/gfxbench
