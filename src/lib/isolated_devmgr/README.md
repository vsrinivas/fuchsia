# Isolated Devmgr

This library contains a set of tools to streamline the creation of components that expose
isolated devmgrs.

## Usage

To create your own version of an isolated devmgr, you first need to define a build rule for it,
which will look like:

```gn
import("//src/lib/isolated_devmgr/devmgr_manifest.gni")

# declare our own manifest
devmgr_manifest("devmgr-manifest") {
}

# declare our isolated devmgr component
package("my_devmgr") {
  testonly = true

  deps = [
    ":devmgr-manifest",
    "//src/lib/isolated-devmgr",
  ]

  extra = [ "$target_gen_dir/devmgr.manifest" ]

  binaries = [
    {
      name = "isolated_devmgr"
    },
  ]

  meta = [
    {
      dest = "my_devmgr.cmx"
      path = dest
    },
  ]
}
```

Then we need to also provide a `cmx` manifest for that new component, called `my_devmgr.cmx`.
The arguments passed to `isolated_devmgr` configure its behavior for your own use case. You can
see all the supported arguments [here](./main.cc). The main arguments you typically will provide
are `svc_name` and `load_driver`. `svc_name` configures the name of the `devfs` namespace that
will be exposed to the component's directory request and `load_driver` (which can be expressed
multiple times) lists drivers to load when it is launched. `device_vid_pid_did` is optional
argument which can be used to create a device.

```json
{
    "program": {
        "args": [
            "--svc_name=fuchsia.example.MyDevmgr",
            "--load_driver=/boot/driver/my_driver1.so",
            "--load_driver=/boot/driver/my_driver2.so",
            "--wait_for=sys/my_driver"
        ],
        "binary": "bin/isolated_devmgr"
    },
    "sandbox": {
        "boot": [
            "bin",
            "driver",
            "lib"
        ],
        "services": [
            "fuchsia.process.Launcher",
            "fuchsia.sys.Launcher",
            "fuchsia.exception.Handler"
        ]
    }
}
```
With this custom isolated devmgr, you can then use it in test cases by either launching it manually
using the fuchsia-url for your package (in this example
`fuchsia-pkg://fuchsia.com/my_devmgr#meta/my_devmgr.cmx`) or you can inject it as a service and
use service injection in your test component's manifest, like so:

```json
{
    "facets": {
      "fuchsia.test": {
       "injected-services" : {
         "fuchsia.example.MyDevmgr": "fuchsia-pkg://fuchsia.com/my_devmgr#meta/my_devmgr.cmx"
       }
      }
    },
    "program": {
        "binary": "test/my_devmgr_test"
    },
    "sandbox": {
        "services": [
           "fuchsia.example.MyDevmgr"
        ]
    }
}
```

