#Using FIDL Proxies with Plugins

FFX plugins can communicate with a target device using FIDL through
[Overnet](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/src/connectivity/overnet/).

Extending the example from the [plugins](plugins.md) page we can add
FIDL proxies to the parameter list for plugins:

First we add a dependency to the plugin's `BUILD.gn` file:

```GN
import("//src/developer/ffx/build/ffx_plugin.gni")

ffx_plugin("ffx_example") {
  version = "0.1.0"
  edition = "2018"
  with_unit_tests = true
  deps = [
    "//sdk/fidl/fuchsia.device:fuchsia.device-rustc",
  ]
}
```

This makes the FIDL proxy bindings available for import in the
plugin. In this case you can now import `fidl_fuchsia_device`.
Start by importing the type.  It is easiest if you include
`fidl_fuchsia_device` either directly or as an alias.

```rust
use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_example_args::ExampleCommand,
    fidl_fuchsia_device as fdevice,
};

```

If you would like to include `NameProviderProxy` directly, you will
also need to include `NameProviderMarker` (even if your code does not
use `NameProviderMarker`):

```rust
use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_example_args::ExampleCommand,
    fidl_fuchsia_device::{NameProviderProxy, NameProviderMarker},
};

```

Now that the type is imported, the proxy can be used in the plugin
function. FFX plugins can accept proxies in the parameter list:

```rust
#[ffx_plugin(
    fdevice::NameProviderProxy = "core/appmgr:out:fuchsia.device.NameProvider"
)]
pub async fn example(
    name_proxy: fdevice::NameProviderProxy,
    _cmd: ExampleCommand,
) -> Result<(), Error> {
    if let Ok(name) = name_proxy.get_device_name().await? {
        println!("Hello, {}", name);
    }
    Ok(())
}
```

In order to correctly connect a proxy to the FIDL service on the
target, you will need to map the proxy type to a component selector
that can be used to find the FIDL service.  More about component
selectors can be found on the [Component Select](component-select.md)
page. This mapping is passed into the ffx_plugin annotation at the top
of the function signature:

```rust
#[ffx_plugin(
    fdevice::NameProviderProxy = "core/appmgr:out:fuchsia.device.NameProvider"
)]
```

And that's it.  The plugin should now be able to communicate with
target device using native FIDL calls.  FFX plugins can accept any
number of plugins as long as the same steps are followed and the
proxies are correctly mapped to component selectors.

There are two exceptions to this rule.  FFX already knows how to
communicate with two proxies without mappings.  It's enough to just
add these proxies to the parameter list without changing the
ffx_plugin annotation:

- [DaemonProxy](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/sdk/fidl/fuchsia.developer.bridge/daemon.fidl) - more info can be found [here](daemon.md)
- [Remote Control Service (RCS)](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/sdk/fidl/fuchsia.developer.remotecontrol/remote-control.fidl) - more info can be found [here](rcs.md)
