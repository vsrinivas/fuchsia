This directory contains source for

1. The input-pipeline component for astro and sherlock (`//src/ui/bin/input-pipeline:component`).
   This component is not yet in production use, but is planned to replace the input
   processing code in root_presenter.

2. The input-pipeline test component (`//src/ui/bin/input-pipeline:component-for-test`).
   This is similar to the regular component, except that its manifest does not
   include `/dev/class/input-report`. Hence, tests using this component will not be
   perturbed by any physical input devices.

   These tests, will, however, log an error message about `/dev/class/input-report`
   being unavailable. To avoid the log message, packages bundling this component
   should include an empty `/config/data/ignore_real_devices`. For an example
   of how to do this, see `//src/ui/tests/integration_input_tests/factory-reset-handler/BUILD.gn`.