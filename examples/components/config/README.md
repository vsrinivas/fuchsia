# Config Example

This directory contains a simple example using structured configuration in
[Component Framework](/docs/concepts/components/introduction.md).

> Note: This example uses an in-development feature which is not yet ready for
widespread usage and requires adding your build targets to an allowlist.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

## Running

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

-  **C++**

```bash
$ ffx component run /core/ffx-laboratory:config_example fuchsia-pkg://fuchsia.com/cpp_config_example#meta/config_example.cm
```

-  **Rust**

```bash
$ ffx component run /core/ffx-laboratory:config_example fuchsia-pkg://fuchsia.com/rust_config_example#meta/config_example.cm
```

When the above command is run, you can see the following output with `fx log`:

```
[00883.647731][187248][187250][ffx-laboratory:config_cpp] INFO: [main.cc(16)] Hello, World!
[00884.025230][187707][187710][ffx-laboratory:config_rust] INFO: Hello, World!
```


Use `ffx component show` to see the configuration of this component:

-  **C++**

```bash
$ ffx component show /core/ffx-laboratory:config_cpp
               Moniker: /core/ffx-laboratory:config_cpp
                   URL: fuchsia-pkg://fuchsia.com/config_example#meta/config_cpp.cm
                  Type: CML dynamic component
       Component State: Resolved
 Incoming Capabilities: fuchsia.logger.LogSink
                        pkg
  Exposed Capabilities: diagnostics
           Merkle root: b1f857abac9df2226468258ffa5d029268df42cc35fea41bb18eb3a26ac4dc22
         Configuration: greeting -> "World"
       Execution State: Running
          Start reason: Instance was started from debugging workflow
         Running since: 2022-03-21 17:17:14.971026803 UTC
                Job ID: 1321890
            Process ID: 1321913
 Outgoing Capabilities: debug
                        diagnostics
```

-  **Rust**

```bash
$ ffx component show /core/ffx-laboratory:config_rust
               Moniker: /core/ffx-laboratory:config_rust
                   URL: fuchsia-pkg://fuchsia.com/config_example#meta/config_rust.cm
                  Type: CML dynamic component
       Component State: Resolved
 Incoming Capabilities: fuchsia.logger.LogSink
                        pkg
  Exposed Capabilities: diagnostics
           Merkle root: b1f857abac9df2226468258ffa5d029268df42cc35fea41bb18eb3a26ac4dc22
         Configuration: greeting -> "World"
       Execution State: Running
          Start reason: Instance was started from debugging workflow
         Running since: 2022-03-21 17:18:02.092634558 UTC
                Job ID: 1339589
            Process ID: 1339614
 Outgoing Capabilities: diagnostics
```

### Overriding values

These examples are able to accept overridden configuration values during
development by adding to your `fx set`:

```
$ fx set core.qemu-x64 \
  --with //examples/components/config:config_example \
  --args='config_example_cpp_greeting="C++ CLI Override"' \
  --args='config_example_rust_greeting="Rust CLI Override"'
```

-  **C++**

```bash
$ ffx component run /core/ffx-laboratory:config_cpp fuchsia-pkg://fuchsia.com/config_example#meta/config_cpp.cm
```

-  **Rust**

```bash
$ ffx component run /core/ffx-laboratory:config_rust fuchsia-pkg://fuchsia.com/config_example#meta/config_rust.cm
```

In `fx log`:

```
[02625.458743][525815][525817][ffx-laboratory:config_cpp] INFO: [main.cc(14)] Hello, C++ CLI Override!
[02625.823269][526271][526273][ffx-laboratory:config_rust] INFO: Hello, Rust CLI Override!
```

## Inspect

These examples also publish their configuration to an Inspect VMO.

- **C++**

```bash
$ ffx inspect show core/ffx-laboratory\*config_cpp
core/ffx-laboratory\:config_cpp:
  metadata:
    filename = fuchsia.inspect.Tree
    component_url = fuchsia-pkg://fuchsia.com/config_example#meta/config_cpp.cm
    timestamp = 7293873723999
  payload:
    root:
      config:
        greeting = World
```

- **Rust**

```bash
$ ffx inspect show core/ffx-laboratory\*config_rust
core/ffx-laboratory\:config_rust:
  metadata:
    filename = fuchsia.inspect.Tree
    component_url = fuchsia-pkg://fuchsia.com/config_example#meta/config_rust.cm
    timestamp = 7364472787546
  payload:
    root:
      config:
        greeting = World
```

## Testing

Integration tests for structured config are available in the `config_integration_test` package.
Use the `ffx test run` command to run the tests on a target device:

```bash
$ ffx test run fuchsia-pkg://fuchsia.com/config_integration_test#meta/config_integration_test.cm
```

You should see an integration test for each language execute and pass:

```
[RUNNING]       inspect_cpp
[RUNNING]       inspect_rust
[PASSED]        inspect_rust
[PASSED]        inspect_cpp

2 out of 2 tests passed...
fuchsia-pkg://fuchsia.com/config_integration_test?hash=9929af411e02ca5d50f6bbef76497439dc9c1eb76a7e8e64facd24e7840ce1a7#meta/config_integration_test.cm completed with result: PASSED
```

NOTE: This test will not pass if the configuration value is overridden.

## Configuration values from a JSON file

Configuration values for a component can be specified using GN args or from a JSON file.
To use packages that get values from a JSON file, append the `_with_json_values` to the package
names used above.

```bash
$ ffx component run /core/ffx-laboratory:config_cpp fuchsia-pkg://fuchsia.com/config_example_with_json_values#meta/config_cpp.cm
```

```bash
$ ffx test run fuchsia-pkg://fuchsia.com/config_integration_test_with_json_values#meta/config_integration_test.cm
```