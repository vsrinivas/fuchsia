# Bluetooth Profile: Fast Pair Provider

This component implements the Google Fast Pair Service (GFPS) Provider role as defined in the
[official specification](https://developers.google.com/nearby/fast-pair/spec).

## Build

Ensure `bt-fastpair-provider` package is in your Fuchsia build. To include it in the universe set
of packages, use the `fx set` configuration or `fx args`. To include it in the base or cached set
of packages, update the product-specific `.gni` file.

`bt-fastpair-provider` provides the [fuchsia.bluetooth.fastpair.Provider](/sdk/fidl/fuchsia.bluetooth.fastpair/provider.fidl)
capability which allows clients to enable/disable the service. Include the
[core shard](/src/connectivity/bluetooth/core/bt-init/meta/bt-fastpair.core_shard.cml) in the
product configuration. For example, for the workstation configuration, add the core shard to the
`core_realm_shards` list in [workstation.gni](/products/common/workstation.gni).

## Configuration

The component relies on [structured configuration](https://fuchsia.dev/fuchsia-src/development/components/configuration/structured_config)
to define a configurable set of program parameters. See the [component manifest](meta/bt-fastpair-provider.cml)
for more details on the configurable parameters.

A product integrator must define a configuration with the appropriate values and include it with
the package. See the [product assembly](https://fuchsia.dev/fuchsia-src/development/components/configuration/assembling_structured_config)
for more information.

For example, define a configuration and a package:

```
fuchsia_structured_config_values("example_config_values") {
  component_name = "bt-fastpair-provider"
  cm_label = "//src/connectivity/bluetooth/profiles/bt-fastpair-provider:manifest"
  values = {
    model_id = 0
    firmware_revision = "1.0.0"
    private_key = "ThisIsAnExamplePrivateKey"
  }
}

fuchsia_package("bt-fastpair-provider") {
  deps = [
    ":example_config_values",
    "//src/connectivity/bluetooth/profiles/bt-fastpair-provider:component",
  ]
}
```

Include the `bt-fastpair-provider` package in the build of your Fuchsia product.

## Testing

Add the following to your Fuchsia configuration to include the component unit tests in your build:

`//src/connectivity/bluetooth/profiles/bt-fastpair-provider:bt-fastpair-provider-tests`

To run the tests:

```
fx test bt-fastpair-provider-tests
```
