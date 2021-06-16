This directory contains various GN build rules to build various
programs for the graphics compute project. See the documentation
in `build_rules.gni` to see how to use them. In a nutshell:

* `graphics_compute_executable()`:

 Used to create a host executable or Fuchsia package + component.
 Supports `needs_vulkan = true` to add Vulkan dependencies automatically.

 These are never run during continuous integration.
 Use '`fx shell run <name>`' or '`out/default/host_x64/<name>`' to run them.

* `graphics_compute_vulkan_executable()`:

 Same as above, sets `needs_vulkan = true` for you as a convenience.

* `graphics_compute_test_package()`:

 Used to create a host test executable, or Fuchsia package + test component.
 Supports `needs_vulkan = true` to add Vulkan dependencies automatically.

 These are always run during continuous integration!
 Use '`fx test <name>`' or '`fx run-host-tests <name>`' to run them.

 IMPORTANT: Vulkan-based host test packages will currently fail on
 CI bots, because these do not provide a valid Vulkan loader + ICD, so
 avoid adding them as dependencies to src/graphics/lib/compute:tests.

* `graphics_compute_unittests()`:

 Convenience rule to define a source set that uses the GoogleTest library.

* `graphics_compute_unittests_package()`:

 Convenience rule to define a `graphics_compute_test_package()` that uses
 GoogleTest's main library. The tests should be provided in dependencies
 that are `graphics_compute_unittests()` targets.
