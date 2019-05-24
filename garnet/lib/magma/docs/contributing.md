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

### Build Configuration for Testing

##### Product for L0 and L1 testing:
* garnet/products/graphics-dev.gni

##### Packages for L0 and L1 testing:
* garnet/packages/tests/magma
* garnet/packages/experimental/disabled/tests_magma
* garnet/packages/tests/image_pipe_swapchain
* garnet/packages/examples/vkcube_on_scenic
* garnet/packages/examples/vkcube_on_fb
* garnet/packages/tools/magma

##### Product for L2 testing:
* topaz/products/default.gni

##### Package for L2 testing:
* topaz/packages/examples/spinning_cube

### Testing Pre-Submit

For details on the testing strategy for magma, see [Test Strategy](test_strategy.md).

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

1. Build with --args magma_enable_developer_build=true; this will run unit tests that require hardware,
then present the device as usual for general applications.  Inspect the syslog for test results.

2. Run the test script [lib/magma/scripts/test.sh](../../../lib/magma/scripts/test.sh) and inspect the test results.

#### L1

If you have an attached display, execute the spinning [vkcube](../../../lib/vulkan/tests/vkcube).
This test uses an imagepipe swapchain to pass frames to the system compositor.  
Build with `--with garnet/packages/examples/vkcube_on_scenic`.
Run the test with `run fuchsia-pkg://fuchsia.com/present_view#meta/present_view.cmx fuchsia-pkg://fuchsia.com/vkcube_on_scenic#meta/vkcube_on_scenic.cmx`.

#### L2

A full UI 'smoke' test. Build the entire product including your change.  

Login as Guest on the device and run both of these commands:
./scripts/fx shell sessionctl  --story_name=spinning_cube --mod_name=spinning_cube --mod_url=spinning_cube add_mod spinning_cube
./scripts/fx shell sessionctl  --story_name=spinning_cube2 --mod_name=spinning_cube2 --mod_url=spinning_cube add_mod spinning_cube

For details, refer to top level project documentation.

#### S0

For stress testing, run the test script [lib/magma/scripts/stress.sh](../../../lib/magma/scripts/stress.sh)
and ensure that the driver does not leak resources over time.

#### S1

A full UI stress test.  Launch the spinning_cube example and the infinite_scroller, and let them run overnight.

#### C0

For some changes, it's appropriate to run the Vulkan conformance test suite before submitting.
See [Conformance](#conformance).

#### P0

For some changes, it's appropriate to run benchmarks to validate performance metrics. See [Benchmarking](#benchmarking).

### Conformance

For details on the Vulkan conformance test suite, see

* [../third_party/vulkan-cts](../../../../third_party/vulkan-cts)

### Benchmarking

The source to Vulkan gfxbench is access-restricted. It should be cloned into third_party.

* https://fuchsia-vendor-internal.googlesource.com/gfxbench

### See Also
* [Test Strategy](test_strategy.md)
