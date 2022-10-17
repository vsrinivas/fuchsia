# Fake-clock

A system enabling a test component to manipulate monotonic time for components
under test.

Fake-clock consists of a library and service component.

The service component maintains a fake monotonic time, which may be accessed
through the [`fuchsia.testing.FakeClock`][fidl] protocol and may be controlled
through the [`fuchsia.testing.FakeClockControl`][fidl] protocol.

The library overrides syscalls defined in the
[vDSO][vdso] that interact with monotonic time or
have a built in deadline. The overrides route time-based waits and signals to
the service component via the [`fuchsia.testing.FakeClock`][fidl] protocol.

In addition, the named-timer crate allows an author to annotate critical
deadlines in a component with names. When linked against the fake-clock
library, the deadline set and expired events are reported to the fake-clock
service. A test author may register interest in these events to stop time when
these critical points are reached.

## Use

A test that wishes to use fake-clock must correctly link the library to each
component in the test and route the [`fuchsia.testing.*`][fidl] protocols.

Add the service component as a dependency in the test package.
```
fuchsia_test_package("my-integ-test") {
  test_components = [ "integ-test-component" ]
  deps = [
    ":component_with_fake_time",
    "//src/lib/fake-clock/svc",
  ]
}
```

Link the library against each binary. Since the library is only available to
test targets, create a new version of the binary that depends on
"//src/lib/fake-clock/lib".
```
# Rust binary
rustc_binary("bin_with_fake_time") {
    ...
    testonly = true
    non_rust_deps = [ "//src/lib/fake-clock/lib" ]
}

# C++ binary
executable("bin_with_fake_time") {
    ...
    testonly = true
    deps = [
        ...
        "//src/lib/fake-clock/lib"
    ]
}

# Component with fake time
fuchsia_component("component_with_fake_time") {
    testonly = true
    component_name = "component_with_fake_time"
    manifest "meta/component_with_fake_time.cml"
    deps = [":bin_with_fake_time"]
}
```

Include the fake-clock shard in the manifest for each component with fake time.
```
    ...
    "include": [
        "//src/lib/fake-clock/lib/client.shard.cml"
    ],
```

Add the [`fuchsia.testing.FakeClockControl`][fidl] protocol to the test
component's manifest.
```
    ...
    "use": [
        {
            "protocol": [
                "fuchsia.testing.FakeClockControl"
            ]
        }
    ]
```

The test may then connect to
[`fuchsia.testing.FakeClockControl`][fidl] to control monotonic time.

See the [examples][examples] directory for example setups.

[vdso]: /docs/concepts/kernel/vdso.md
[fidl]: fidl/fake_clock.fidl
[examples]: examples/
