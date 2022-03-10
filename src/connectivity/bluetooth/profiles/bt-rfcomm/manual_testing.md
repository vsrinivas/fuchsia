# Manual Testing bt-rfcomm

The `bt-rfcomm` component exposes a testonly capability: `fuchsia.bluetooth.rfcomm.test.RfcommTest`.
This capability should only be used in test configurations.

By default, this capability is not exposed to the system. In order to expose it, update the build:

* Include the [`core-testonly` Bluetooth group](../../BUILD.gn) in the relevant `product.gni` file,
e.g

```
  base_package_labels -= ["//src/connectivity/bluetooth:core"]
  base_package_labels += ["//src/connectivity/bluetooth:core-testonly"]
```

* Update [`core.cml`](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/sys/core/meta/core.cml) to
point to `bt-init-testonly.cm`. This constructs the bluetooth-core realm with the testonly
capability routes.

* Include the [`bt-init-testonly-core-shard`](../../core/bt-init/meta/bt-init-testonly.core_shard.cml)
in your `product.gni` target. This allows v1 components (e.g SL4F, BT-tools) to access the
`RfcommTest` capability.
