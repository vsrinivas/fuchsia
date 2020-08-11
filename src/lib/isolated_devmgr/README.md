# Isolated Devmgr

This library contains a set of tools to streamline the creation of components
that expose isolated devmgrs.

## Usage

To create your own version of an isolated devmgr, you first need to define a
build rule for it, which will look like:

```gn
import("//src/lib/isolated_devmgr/isolated_devmgr.gni")

isolated_devmgr_component("my-devmgr") {
  args = [
    "--svc_name=fuchsia.example.MyDevmgr",
    "--load_driver=/boot/driver/my_driver1.so",
    "--load_driver=/boot/driver/my_driver2.so",
    "--wait_for=sys/my_driver"
  ]

  deps = [
    "//src/devices/tests/sysdev",
    "//path/to/my/driver:my-driver",
  ]
}
```

The `args` parameter configures its behavior for your own use case. You can see
all the supported arguments [here](./main.cc). The main arguments you typically
will provide are `svc_name` and `load_driver`. `svc_name` configures the name of
the `devfs` namespace that will be exposed to the component's directory request
and `load_driver` (which can be expressed multiple times) lists drivers to load
when it is launched. `device_vid_pid_did` is optional argument which can be used
to create a device.

With this custom isolated devmgr, you can then use it in test cases by either
launching it manually using the fuchsia-url for your package (in this example
`fuchsia-pkg://fuchsia.com/my-package#meta/my-devmgr.cmx`) or you can inject it
as a service and use service injection in your test component's manifest, like
so:

```json
{
    "facets": {
      "fuchsia.test": {
       "injected-services" : {
         "fuchsia.example.MyDevmgr": "fuchsia-pkg://fuchsia.com/my-package#meta/my-devmgr.cmx"
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

In either case you will want to make sure to list the isolated devmgr as a `dep`
for your test component.


## Using inspect with isolated devmgr

The device tree and the diagnostics are hosted in the outgoing directory of the test component.
In order to view the inspect files -
1. Add a breakpoint to the test using [zxdb](https://fuchsia.dev/fuchsia-src/development/debugger/debugger_usage).
2. When breakpoint is hit, open `fx shell` and find the hub path to the test component.
  E.g:
  ```
  find /hub -name fs-management-devmgr.cmx
  /hub/r/test_env_af569d6f/31969/c/fs-management-devmgr.cmx
  ```
3. You can view the inspect file using iquery.
  E.g:
  ```
  fx iquery show-file /hub/r/test_env_af569d6f/31969/c/fs-management-devmgr.cmx/32468/out/dev/diagnostics/driver_manager/dm.inspect
  ```
