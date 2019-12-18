
# Test Adapters

GTest Adapter is a trampoline that launches gtest binaries, parses output and
translates it to fuchsia.test.Suite protocol. This will be ported into a runner.

## Building

```bash
fx set core.x64 --with //src/sys/test_adapters/gtest
fx build
```

You can build it with fx but it doesn't give much value. Look at [usage](#usage)
section for real value of this trampoline.

## Usage

Lets take an example. A simple normal v2 test will look like

BUILD.gn
```gn
import("//build/package.gni")

executable("bin") {
  output_name = "simple_example"
  testonly = true
  sources = [
    "simple_example.cc",
  ]

  deps = [
    "//src/lib/fxl/test:gtest_main",
  ]
}

package("simple_example") {
  testonly = true
  deps = [
    ":bin",
  ]

  tests = [
    {
      name = "simple_example"
    },
  ]

  meta = [
    {
      path = "meta/simple_example.cml"
      dest = "simple_example.cm"
    },
  ]
}
```

cml file
```cml
{
    "program": {
        "binary": "bin/simple_example",
    },
    "use": [
        {
            "service_protocol": "/svc/fuchsia.logger.LogSink",
        },
    ],
    "expose": [
        {
            "service_protocol": "/svc/fuchsia.test.Suite",
            "from": "self",
        },
    ],
}
```

Above example test will implement `fuchsia.test.Suite` and expose it. To run
gtests without modification, you need to wrap your test in the trampoline. See
below for modified build and cml file.

BUILD.gn
```gn
import("//build/package.gni")

executable("bin") {
  output_name = "simple_example"
  testonly = true
  sources = [
    "simple_example.cc",
  ]

  deps = [
    "//src/lib/fxl/test:gtest_main",
    "//src/sys/test_adapters/gtest", # added dep
  ]
}

package("simple_example") {
  testonly = true
  deps = [
    ":bin",
  ]

  # added gtest_adapter to bin/
  binaries = [
    {
      name = "gtest_adapter"
    },
  ]

  tests = [
    {
      name = "simple_example"
    },
  ]

  meta = [
    {
      path = "meta/simple_example.cml"
      dest = "simple_example.cm"
    },
  ]
}
```

cml file

```cml
{
    "program": {
        // use gtest to run your test
        "binary": "bin/gtest_adapter",
        // pass test as argument
        "args": ["/pkg/test/simple_example"]
    },
    "use": [
        {
             // gtest adapter needs this
            "service_protocol": "/svc/fuchsia.process.Launcher",
        },
        {
            "service_protocol": "/svc/fuchsia.logger.LogSink",
        },
        { // gtest adapter needs this
            "directory": "/tmp",
            "rights": ["rw*"],
        },
    ],
    "expose": [
        {
            "service_protocol": "/svc/fuchsia.test.Suite",
            "from": "self",
        },
    ],
}
```


After these changes you can just run

```
fx run-test simple-test
```

And your test will run as a v2 test.

## Limitations

Currently test adapter doesn't support:

- Disabled tests.
- Tests writing to stdout, those tests can be executed but stdout is lost.

These limitations would be either fixed in this adpater or once we port it to
runners.

## Examples

Two examples to demonstrate how to write v2 test

- [Complex test (injects echo service)](tests/echo-example)
- [Simple test](tests/simple-example)


## Testing

Run:

```bash
fx run-test gtest_adapter_tests
fx run-test gtest_adapter_integration_test
```