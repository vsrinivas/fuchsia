# Manual Testing bt-rfcomm

The `bt-rfcomm` component exposes a testonly capability: `fuchsia.bluetooth.rfcomm.test.RfcommTest`.
This capability should only be used in test configurations.

By default, this capability is not exposed to the system. In order to expose it, update the build:

* Add the [`core-testonly` Bluetooth group](../../BUILD.gn#53) to the set of base packages in
[core.gni](https://fuchsia.googlesource.com/fuchsia/+/HEAD/products/core.gni).
e.g. Replace `//src/connectivity/bluetooth:core` with `//src/connectivity/bluetooth:core-testonly`.

* Add the [bt-init testing shard](../../core/bt-init/meta/bt-init-testonly.core_shard.cml)
to the group of [core_realm_shards](https://fuchsia.googlesource.com/fuchsia/+/HEAD/products/core.gni).
This allows v1 components (e.g SL4F, BT-tools) to access the `RfcommTest` capability.


* Update the `bluetooth-core` realm in [`core.cml`](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/sys/core/meta/core.cml)
to point to `"fuchsia-pkg://fuchsia.com/bt-init-testonly#meta/bt-init-testonly.cm"`. This constructs
the `bluetooth-core` realm with the testonly capability routes.
