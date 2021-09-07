# Legacy Test Runner

Reviewed on: 2021-08-11

Legacy test runner is a [test runner][test-runner] that launches a wrapped legacy
test component in a new hermetic enclosing environment.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/legacy_test_runner
fx build
```

## Wrapping legacy tests

To wrap your legacy test, add legacy manifest file path to your
`program.legacy_manifest` along with necessary shards:

simple_test.cml:

```cml
{
    include: [
        "//src/sys/test_runners/legacy_test/default.shard.cml",
    ],
    program: {
        legacy_manifest: "meta/simple_test.cmx",
    },
}
```

Then change your BUILD.gn to create a new component and add it as a test to your
package:

```gn
fuchsia_test_component("simple_test_legacy") {
  component_name = "simple_test"
  manifest = "meta/simple_test.cmx"
  deps = [ ":simple_test_bin" ]
}

fuchsia_test_component("simple_test_modern") {
  component_name = "simple_test"
  manifest = "meta/simple_test.cml"
}

fuchsia_package("simple_test") {
  testonly = true
  test_component =[ ":simple_test_modern" ]
  deps = [
    ":simple_test_legacy", # simple_test_legacy is no longer added as "test_component".
  ]
}

```

## Concurrency

The legacy test is treated as one test case and run as such, so there is
no concept of concurrency. If you want to control concurrency of test cases in
your legacy test,  pass the appropriate option as an argument.

For example to run your rust test with concurrency of 5 run:

```bash
fx test <test> -- --test-threads=5
```

Note: This test runner will ignore `--parallel` flag passed using [this guide](override-parallel).

## Arguments

See [passing-arguments](passing-arguments) to learn more.

## Limitations

- Cannot run legacy tests which execute in sys realm.
- `use` declarations are not supported except for the following protocols: Log, LogSink, ArchiveAccessor

## Testing

Run:

```bash
fx test legacy-test-runner-integration-test
fx test legacy-test-runner-integration-test
```

## Source layout

The entry point is located in `main.cc`, the FIDL service implementation and
all the test logic exists other source files. Integration tests are located in `tests` folder.

[test-runner]: ../README.md
[override-parallel]: /docs/concepts/testing/modern/test_component.md#running_test_cases_in_parallel
[passing-arguments]: /docs/concepts/testing/modern/test_runner_framework.md#passing_arguments
