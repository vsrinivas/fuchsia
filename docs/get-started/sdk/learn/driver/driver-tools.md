# Interact with the driver

Fuchsia components interact with driver components through their exposed entries
in [devfs][concepts-devfs]. Once a client component connects to the driver's
devfs entry, it receives an instance of the FIDL service representing that
driver.

In this section, you'll create a new `eductl` component that discovers and
interacts with the capabilities exposed by the `qemu_edu` driver.

## Create a new tools component

Create a new project directory in your Bazel workspace for a new tools component:

```posix-terminal
mkdir -p fuchsia-codelab/qemu_edu/tools
```

After you complete this section, the project should have the following directory
structure:

```none {:.devsite-disable-click-to-copy}
//fuchsia-codelab/qemu_edu/tools
                  |- BUILD.bazel
                  |- meta
                  |   |- eductl.cml
                  |- eductl.cc
```

Create the `qemu_edu/tools/BUILD.bazel` file and add the following statement to
include the necessary build rules from the Fuchsia SDK:

`qemu_edu/tools/BUILD.bazel`:

```bazel
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/BUILD.bazel" region_tag="imports" adjust_indentation="auto" %}
```

Create a new `qemu_edu/tools/meta/eductl.cml` component manifest file to the
project with the following contents:

`qemu_edu/tools/meta/eductl.cml`:

```json5
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/meta/eductl.cml" region_tag="example_snippet" adjust_indentation="auto" %}

```

This component requests the `dev` directory capability, which enables it to
discover and access entries in devfs. Create a new `qemu_edu/tools/eductl.cc`
file with the following code to set up a basic command line executable:

`qemu_edu/tools/eductl.cc`:

```cpp
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/eductl.cc" region_tag="imports" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/eductl.cc" region_tag="cli_helpers" adjust_indentation="auto" %}

int main(int argc, char* argv[]) {
  const char* cmd = basename(argv[0]);

  // ...

  return usage(cmd);
}

```

This executable supports two subcommands to execute the liveness check and
factorial computation.

Add the following new rules to the bottom of the project's build configuration
to build this new component into a Fuchsia package:

`qemu_edu/tools/BUILD.bazel`:

```bazel
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/BUILD.bazel" region_tag="binary" adjust_indentation="auto" exclude_regexp="\/\/src\/qemu_edu" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/BUILD.bazel" region_tag="component" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/BUILD.bazel" region_tag="package" adjust_indentation="auto" %}
```

## Implement the client tool

When client components open a connection to an entry in devfs, they receive an
instance of the FIDL protocol being served by the driver. Add the following code
to the tools component to open a connection to the `edu` device using its devfs
path:

`qemu_edu/tools/eductl.cc`:

```cpp
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/eductl.cc" region_tag="imports" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/eductl.cc" region_tag="fidl_imports" adjust_indentation="auto" highlight="1" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/eductl.cc" region_tag="device_path" adjust_indentation="auto" highlight="1,2" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/eductl.cc" region_tag="device_client" adjust_indentation="auto" highlight="1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18" %}

// ...
```

Add `liveness_check()` and `compute_factorial()` functions to call methods using
the `fuchsia.hardware.qemuedu/Device` FIDL protocol returned from `OpenDevice()`.
Finally, update the tool's `main()` function to call the appropriate device
function based on the argument passed on the command line:

`qemu_edu/tools/eductl.cc`:

```cpp
// ...

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/eductl.cc" region_tag="liveness_check" adjust_indentation="auto" highlight="1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/eductl.cc" region_tag="compute_factorial" adjust_indentation="auto" highlight="1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/eductl.cc" region_tag="main" adjust_indentation="auto" highlight="4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20" %}
```

Update the tools component's build configuration to depend on the FIDL bindings
for the `fuchsia.hardware.qemuedu` library:

`qemu_edu/tools/BUILD.bazel`:

{% set build_bazel_snippet %}
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/tools/BUILD.bazel" region_tag="binary" adjust_indentation="auto" highlight="7" %}
{% endset %}

```bazel
{{ build_bazel_snippet|replace("//src/qemu_edu","//fuchsia-codelab/qemu_edu")|trim() }}
```

<<_common/_restart_femu.md>>

## Reload the driver

Use the `bazel run` command to build and execute the driver component target:

```posix-terminal
bazel run --config=fuchsia_x64 //fuchsia-codelab/qemu_edu/drivers:pkg.component
```

## Run the tool

Use the `bazel run` command to build and execute the tools component target:

```posix-terminal
bazel run --config=fuchsia_x64 //fuchsia-codelab/qemu_edu/tools:pkg.component
```

The `bazel run` command performs the following steps:

1.  Build the component and package.
1.  Publish the package to a local package repository.
1.  Register the package repository with the target device.
1.  Use `ffx component run --recreate` to run the component inside the
    [`ffx-laboratory`][ffx-laboratory].

Inspect the system log and verify that you can see the driver responding to a
request from the `eductl` component, followed by the tool printing the result:

```posix-terminal
ffx log --filter qemu_edu --filter eductl
```

```none {:.devsite-disable-click-to-copy}
[universe-pkg-drivers:root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_][qemu-edu,driver][I]: [fuchsia-codelab/qemu_edu/qemu_edu.cc:232] Replying with factorial=479001600
[ffx-laboratory:eductl][][I] Factorial(12) = 479001600
```

Congratulations! You've successfully connected to your driver from a separate
client component using its exposed services.

<!-- Reference links -->

[concepts-devfs]: /docs/concepts/drivers/driver_communication.md#service_discovery_using_devfs
[ffx-laboratory]: /docs/development/components/run.md#ffx-laboratory
