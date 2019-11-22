# Testing Scenic and Escher

## Testability

Information about testability:

* All changes within Fuchsia need to adhere to the [Testability rubric](/docs/development/testing/testability_rubric.md).
* See also: [general Fuchsia testing documentation](/docs/development/testing/environments.md)

## Scenic test packages

You can specify packages in these ways:

* Everything:

  ```
  --with //bundles:tests
  ```

* Individually, if you want to build less packages:

  ```
  --with //garnet/packages/tests:scenic
  --with //garnet/packages/tests:scenic_cpp
  --with //garnet/packages/tests:escher
  --with //garnet/packages/tests:ui
  --with //garnet/packages/tests:e2e_input_tests
  --with //garnet/packages/tests:vulkan
  --with //garnet/packages/tests:magma
  ```

## Scenic and CQ

Trybots run tests automatically before submission of every change in Fuchsia. See the "fuchsia-x64-release" and "fuchsia-x64-debug" bots on
[https://ci.chromium.org/p/fuchsia/builders](https://ci.chromium.org/p/fuchsia/builders).

## Unit tests and integration tests

To run tests locally during development:

### Running on device

Some of these tests require the test Scenic to connect to the real display controller.

Run `fx shell killall scenic.cmx` to kill an active instance of Scenic.

* Run all Scenic tests:

  From host workstation, ensure `fx serve` is running, then:

  ```
  fx run-test scenic_tests
  fx run-test escher_tests
  fx run-test flutter_screencap_test
  ```

  From Fuchsia target device:

  ```
  runtests -t gfx_apptests,gfx_unittests,escher_unittests,input_unittests,a11y_manager_apptests
  ```

* Run a specific test binary:

  From host workstation, ensure `fx serve` is running, then:

  ```
  fx run-test scenic_tests -t gfx_unittests  # -t <test binary name>
  ```

  From Fuchsia target device:

  ```
  runtests -t gfx_unittests
  ```

* Run a single test:

  From host workstation, ensure `fx serve` is running, then:

  ```
  fx run-test scenic_tests -t gfx_unittests -- --gunit_filter=HostImageTest.FindResource
  ```

  From Fuchsia target device:

  ```
  runtests -t gfx_unittests -- --gtest_filter=HostImageTest.FindResource
  ```

  See more documentation about the [glob pattern for the filter arg](https://github.com/google/googletest/blob/master/googletest/docs/advanced.md).

* Run a specific component

  From your host workstation:

  ```
  fx shell run-test-component fuchsia-pkg://fuchsia.com/scenic_tests#meta/gfx_unittests.cmx
  ```

  Note: `gfx_unittests.cmx` can be swapped for [any test component](/src/ui/scenic/BUILD.gn) . There is also fuzzy matching!

* Pixel tests

  If you get an error connecting to a display controller, first kill all UI services.

  From your host workstation, run:

  ```
  fx shell "killall base_mgr.cmx; killall root_presenter.cmx; killall scenic.cmx; killall tiles.cmx; killall present_view"
  ```

  Then run the pixel tests:

  ```
  fx shell run fuchsia-pkg://fuchsia.com/scenic_tests#meta/gfx_pixeltests.cmx
  ```

  Alternatively, run:

  ```
  fx shell runtests -t gfx_pixeltests
  ```

  Note: `gfx_pixeltests` currently requires `//bundles:tests` to be in your list of packages (e.g., `fx set [...] --with //bundles:tests`)

### Running on emulator

From your host workstation:

```
fx set terminal.x64 --release --with-base //garnet/packages/tests:scenic
```

Then, start an emulator:

* Start QEMU:

  ```
  fx qemu
  ```

* Start AEMU:

  ```
  fx emu -N
  ```

Then, in the QEMU or EMU shell:

```
runtests -t gfx_apptests,gfx_unittests,escher_unittests,input_unittests,a11y_manager_apptests
```

### Host tests

* `fx run-host-tests` will run all the host tests, but you probably only want to run Escher tests.
*  Escher: To run `escher_unittests` locally on Linux: follow instructions in [escher/README.md](/src/ui/lib/escher/README.md).
