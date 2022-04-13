# Dart Test Wrapper

_This is a temporary workaround to enable Fuchsia tests of Component Framework
v2 components written in Dart_

1. Add CML files to the `meta/` directory, for both the Dart test component and
   the wrapper component.

The dart test component may `include` common shards and standard CML
declarations, but _not_ the typical `test_manager` or `test_runner` shards.
The testing shards will be included in the test wrapper component. For example:

**my_dart_test.cml**
```cml
{
    include: [
        "sys/component/realm_builder.shard.cml",
        "syslog/client.shard.cml",
    ],
    use: [
        { protocol: "fuchsia.sys2.EventSource" },
        {
            event: [
                "started",
                "stopped",
            ],
            from: "framework",
        },
    ],
}
```

The wrapper component can `include` default CML declarations from
`dart_test_wrapper.shard.cml`, but must define a unique program binary name.

**my_dart_test_wrapper.cml**
```cml
{
    include: [ "//src/dart/testing/dart_test_wrapper.shard.cml" ],
    program: {
        binary: "bin/my_dart_test_wrapper",
    },
}
```

2. Add build targets for the `dart_test_component` and
   `dart_test_wrapper_component`:

```gn
import("//build/dart/dart_test_component.gni")
import("//src/dart/testing/dart_test_wrapper_component.gni")

dart_test_component("my-dart-test") {
  manifest = "meta/my_dart_test.cml"

  null_safe = true

  # NOTE: The default path to dart test `sources` is the `test/` subdirectory
  sources = [ "my_dart_test.dart" ]

  deps = [
    "//sdk/dart/fidl",
    "//sdk/dart/fuchsia_component_test",
    "//sdk/dart/fuchsia_logger",
    "//sdk/dart/fuchsia_services",
    "//sdk/fidl/fuchsia.component",
    "//sdk/fidl/fuchsia.component.test",
    "//sdk/fidl/fuchsia.logger",
    "//sdk/fidl/fuchsia.sys2",
    "//third_party/dart-pkg/pub/mockito",
    "//third_party/dart-pkg/pub/test",
    ...
  ]
}

dart_test_wrapper_component("my-dart-test-wrapper") {
  wrapper_binary = "my_dart_test_wrapper"
  manifest = "meta/my_dart_test_wrapper.cml"
  dart_test_component_name = "my-dart-test"
}
```

3. Add the wrapper component and the Dart test component to the same
   `fuchsia_test_package`:

```gn
# Run with `fx test my-test-package`.
fuchsia_test_package("package") {
  package_name = "my-test-package"
  test_components = [ ":my-dart-test-wrapper" ]
  deps = [ ":my-dart-test" ]
}
```