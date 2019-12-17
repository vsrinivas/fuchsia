# Rust Test Adapter

Rust Test Adapter is a v2 component that launches a Rust test binary and accepts
the resulting JSON. It translates that JSON result to the Fuchsia Test Service
protocol and passes that response back to [component_manager_for_test](). This is
a temporary component that will be replaced by the Rust Test Runner.

## Building

```
fx set core.x64 --with //bundles/buildbot:core --with //src/sys/test_adapters/rust
fx build
fx update
```

## Running

To use the Rust Test Adapter to run your tests you'll need to correctly configure
your BUILD.gn and cml files.

The BUILD.gn package will include your binaries and tests like this.
```
package("my_rust_package") {
  deps = [
    ":bin",
    ":bin_test",
    "//src/sys/test_adapters/rust",
  ]

  binaries = [
    {
      name = "my_rust_package"
    },
    {
      name = "rust_test_adapter"
    },
  ]

  tests = [
    {
      name = "my_rust_package_bin_test"
    },
  ]

  meta = [
    {
      path = "meta/my_rust_package_bin_test.cml"
      dest = "my_rust_package_bin_test.cm"
    },
  ]
}
```

In the cml file use the Rust Test Adapter as the binary and pass the url to your
package in the args for the program. You will also need to `use` both `fuchsia.process.Launcher`
and `fuchsia.logger.LogSink`.
```
{
    "program": {
        "binary": "bin/rust_test_adapter",
        "args": [
            "test/my_rust_package",
        ]
    },

    "use": [
        {
            "service_protocol": "/svc/fuchsia.process.Launcher",
        },
        {
            "service_protocol": "/svc/fuchsia.logger.LogSink",
        },
    ],
}
```

## Testing

Use the following `fx set` to include the unit tests:
```
fx set core.x64 --with //bundles/buildbot:core
```
Launch the tests with the following command:
```
fx run-test my_rust_package
```

## Limitations

Currently the fuchsia.test.Suite protocol only has statuses for `Passed` and
`Failed`. In the future it would be beneficial to add additional statuses such as
`Ignored`, `Errored`, etc.
