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

### Pre-submit Testing

Magma adheres to a philosophy of multi-level testing.  Unit testing should accompany all units of implementation.
Unit tests can be found:

* [lib/magma/tests/unit_tests](../../../lib/magma/tests/unit_tests)
* [drivers/gpu/msd-intel-gen/tests/unit_tests](../../../drivers/gpu/msd-intel-gen/tests/unit_tests)

Integration tests are the next level of testing:

* [lib/magma/tests/integration](../../../lib/magma/tests/integration)

Particularly, for each of the magma abis, there should be adequate coverage from unit and/or integration tests:

* [include/magma_abi/magma.h](../include/magma_abi/magma.h)
* [include/msd_abi/msd.h](../include/msd_abi/msd.h)

Tests that exercise the vulkan api should also be run on most changes.
Many of these can be executed on a running system using the script:

* [lib/magma/scripts/test.sh](../../../lib/magma/scripts/test.sh)

Which executes the tests defined in:

* [lib/magma/scripts/autorun](../../../lib/magma/scripts/autorun)

The vulkan cube is a good simple, single vulkan application test case:

* [lib/magma/tests/vkcube](../../../lib/magma/tests/vkcube)

For some changes, it's appropriate to run the vulkan conformance test suite before submitting.
See [Conformance](#conformance).

For some changes, it's appropriate to run benchmarks to validate performance metrics. See [Benchmarking](#benchmarking).

For some changes, it's appropriate to build and exercise the full ui to validate functionality and performance.
For details, refer to top level project documentation.

### Conformance ###

For details on the Vulkan conformance test suite, see

* [../third_party/vulkan_loader_and_validation_layers](../../../../third_party/vulkan_loader_and_validation_layers)

See helper scripts:

* [lib/magma/scripts/vulkancts](../../../lib/magma/scripts/vulkancts)

### Benchmarking ###

The source to Vulkan gfxbench is access-restricted. It should be cloned into third_party.

* https://fuchsia-vendor-internal.git.corp.google.com/gfxbench

See helper build script:

* [lib/magma/scripts/build-gfxbench.sh](../../../lib/magma/scripts/build-gfxbench.sh)
