# Input Pipeline

This directory contains source for the input-pipeline component for astro and
sherlock (`//src/ui/bin/input-pipeline:component`).

This component is being repurposed for use on headless devices and replaced by
the [Scene Manager component](../scene_manager/README.md) for devices that can
be used with a screen.

Please reach out to the OWNERS to coordinate any intended work related to this
component and its current or future responsibilities.

# Usage

This component registers the following Input Handlers:

-   `TouchInjectorHandler` for touchscreen input
-   `MediaButtonsHandler` for button input (e.g. volume up and volume down)
-   `FactoryResetHandler` for factory resetting the device

# Structured configuration

This component can be configured during product assembly via the following
configuration schema to define a specific idleness threshold for
[activity recency detection](../../lib/input_pipeline/docs/activity.md):

```
product_assembly_configuration("my_product") {
  platform = {
    input = {
      idle_threshold_minutes = 15
    }
  }
}
```

The `component-for-test` target also sets a default value for testing:

```
fuchsia_structured_config_values("test_config") {
  cm_label = ":manifest"
  values = {
    idle_threshold_minutes = 2
  }
}
```

# Domain package configuration

This component is also configured to support light sensor devices. Products may
want to provide calibration information to make use of light sensing features.
By default we provide empty calibration information, which disables light sensor
processing.

## CML file and integration tests

The production package `//src/ui/bin/input-pipeline:input-pipeline` includes
`meta/input-pipeline.cml`, which exists to serves routes related to input
through human interface devices.

Generally, test packages should include their own copy of a component to ensure
hermeticity with respect to package loading semantics, and can do so by
including `//src/ui/bin/input-pipeline:component-for-test`.
