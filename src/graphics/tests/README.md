# Graphics tests

This directory contains automated tests that cover Fuchsia's graphics stack,
mostly via [the Vulkan API][vulkan-api]. C++ tests should prefer
[Vulkan-Hpp][vulkan-hpp] to direct use of the Vulkan C API.

The tests in this directory cover Fuchsia-specific Vulkan functionality.
Graphics tests against the Vulkan API that don't have any Fuchsia-specific bits
should be submitted to [Vulkan's Conformance Test Suite (CTS)][vulkan-cts].

[vulkan-api]: https://www.vulkan.org/
[vulkan-cts]: https://github.com/KhronosGroup/VK-GL-CTS/blob/main/external/vulkancts/README.md
[vulkan-hpp]: https://github.com/KhronosGroup/Vulkan-Hpp
