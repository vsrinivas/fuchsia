# Bind to the device node

To provide services for devices in a Fuchsia system, drivers must bind to device nodes.
The [driver manager][concepts-driver-manager] maintains the topology of nodes, where each
node represents access to a hardware or virtual device in the system. Once bound to a device
node, the driver can start providing services for the device that the node represents.

The framework matches drivers to device nodes by correlating the
[binding properties][concepts-node-properties] of each node with the set of bind rules provided
by the driver. Bind rules are a set of logic rules that describe which device properties the
driver supports.

In this section, you'll create a skeleton driver that binds to the `edu` device and implements the
bare driver framework hooks.

Note: For more details on the binding process and bind rules, see
[Driver binding][concepts-driver-binding].

## Create a new driver component

To begin, create a new project directory in your Bazel workspace for a driver component called
`qemu_edu`:

```posix-terminal
mkdir -p fuchsia-codelab/qemu_edu
```

After you complete this section, the project should have the following directory structure:

```none {:.devsite-disable-click-to-copy}
//fuchsia-codelab/qemu_edu
                  |- BUILD.bazel
                  |- meta
                  |   |- qemu_edu.cml
                  |- qemu_edu.bind
                  |- qemu_edu.cc
                  |- qemu_edu.h
```

Create the `qemu_edu/BUILD.bazel` file and add the following statement to include the necessary
build rules from the Fuchsia SDK:

`qemu_edu/BUILD.bazel`:

```bazel
load(
    "@rules_fuchsia//fuchsia:defs.bzl",
    "fuchsia_cc_binary",
    "fuchsia_cc_test",
    "fuchsia_component",
    "fuchsia_component_manifest",
    "fuchsia_driver_bytecode_bind_rules",
    "fuchsia_driver_component",
    "fuchsia_fidl_library",
    "fuchsia_fidl_llcpp_library",
    "fuchsia_package",
    "fuchsia_test_package",
    "if_fuchsia",
)
```

## Add the component manifest

The component manifest file defines the attributes of the component's executable, including binding
rules and the component's capabilities. Drivers are loaded as shared libraries (`.so`) using the
`driver` runner.

Create the `qemu_edu/meta/qemu_edu.cml` file and add the following:

`qemu_edu/meta/qemu_edu.cml`:

```json5
{
    include: [
        "syslog/client.shard.cml",
    ],
    program: {
        runner: 'driver',
        binary: 'lib/libqemu_edu.so',
        bind: 'meta/bind/qemu_edu.bindbc'
    },
}

```

Add the following build rules to the bottom of your `qemu_edu/BUILD.bazel` file to compile the
component manifest:

*   `fuchsia_component_manifest()`: Describes the source file and dependencies to compile the
    driver's [component manifest][concepts-component-manifest].

`qemu_edu/BUILD.bazel`:

```bazel
fuchsia_component_manifest(
    name = "manifest",
    src = "meta/qemu_edu.cml",
    includes = [
        "@fuchsia_sdk//pkg/syslog:client",
    ],
)
```

## Configure bind rules

The bind rules describe which device nodes this driver can support. These are listed as a series
of condition statements that reference the key/value pairs in the device node's binding properties.
For a driver to be considered a match, all rules must evaluate to true for the given device node.

Create `qemu_edu/qemu_edu.bind` and add the following bind rules to declare the driver supports PCI
devices with a VID and DID matching the `edu` device:

`qemu_edu/qemu_edu.bind`:

```none
using fuchsia.pci;

fuchsia.BIND_FIDL_PROTOCOL == fuchsia.pci.BIND_FIDL_PROTOCOL.DEVICE;
fuchsia.BIND_PCI_VID == 0x1234;
fuchsia.BIND_PCI_DID == 0x11e8;
```

Add the following build rules to the bottom of your `qemu_edu/BUILD.bazel` file to compile the
bind rules:

*   `fuchsia_driver_bytecode_bind_rules()`: Describes the specifics of the
    [bind rules][concepts-driver-binding] for a driver. The `rules` attribute specifies the file
    that contains the bind rules of this driver. The `deps` attribute specifies the
    [bind libraries][concepts-bind-libraries] to be used with the bind rules specified in rules.

`qemu_edu/BUILD.bazel`:

```bazel
fuchsia_driver_bytecode_bind_rules(
    name = "bind_bytecode",
    output = "qemu_edu.bindbc",
    rules = "qemu_edu.bind",
    deps = [
        "@fuchsia_sdk//bind/fuchsia.pci",
    ],
)
```

## Implement driver hooks

All driver components are required to supply the following two public functions:

*   `Name()`: Used by the driver framework to identify the driver (for instance, to identify the
    driver’s placement in the topology).
*   `Start()`: This is the main function that gets executed as the start hook for the driver.

Once a driver is bound, the framework loads the component binary and calls the static `Start()`
method on the driver class registered using the `FUCHSIA_DRIVER_RECORD_CPP_V1()` macro.

Create `qemu_edu/qemu_edu.h` and `qemu_edu/qemu_edu.cc` and add the following boilerplate code to
create the driver class and configure the initial `Start()` hook:

`qemu_edu/qemu_edu.h`:

```cpp
#ifndef FUCHSIA_CODELAB_CC_QEMU_EDU_H_
#define FUCHSIA_CODELAB_CC_QEMU_EDU_H_

#include <lib/async/dispatcher.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/record_cpp.h>
#include <lib/driver2/structured_logger.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>
#include <lib/zx/status.h>

namespace qemu_edu {

class QemuEduDriver {
 public:
  QemuEduDriver(async_dispatcher_t* dispatcher,
                fidl::WireSharedClient<fuchsia_driver_framework::Node> node,
                driver::Namespace ns,
                driver::Logger logger)
      : outgoing_(component::OutgoingDirectory::Create(dispatcher)),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)) {}
  
  virtual ~QemuEduDriver() = default;

  // Report driver name to the framework
  static constexpr const char* Name() { return "qemu-edu"; }
  // Start hook called by the framework
  static zx::status<std::unique_ptr<QemuEduDriver>> Start(
      fuchsia_driver_framework::wire::DriverStartArgs& start_args,
      fdf::UnownedDispatcher dispatcher,
      fidl::WireSharedClient<fuchsia_driver_framework::Node> node,
      driver::Namespace ns,
      driver::Logger logger);

 private:
  zx::status<> Run(async_dispatcher* dispatcher,
                   fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir);
  
  component::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fuchsia_driver_framework::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;
};

}  // namespace qemu_edu

#endif  // FUCHSIA_CODELAB_CC_QEMU_EDU_H_

```

`qemu_edu/qemu_edu.cc`:

```cpp
#include "fuchsia-codelab/qemu_edu/qemu_edu.h"

namespace qemu_edu {

// Static start hook called by the driver framework on bind
zx::status<std::unique_ptr<QemuEduDriver>> QemuEduDriver::Start(
    fuchsia_driver_framework::wire::DriverStartArgs& start_args,
    fdf::UnownedDispatcher dispatcher,
    fidl::WireSharedClient<fuchsia_driver_framework::Node> node,
    driver::Namespace ns, driver::Logger logger) {

  // Create a unique driver instance
  auto driver = std::make_unique<QemuEduDriver>(
      dispatcher->async_dispatcher(), std::move(node),
      std::move(ns), std::move(logger));
  // Initialize the driver instance
  auto result = driver->Run(dispatcher->async_dispatcher(),
      std::move(start_args.outgoing_dir()));
  if (result.is_error()) {
    return result.take_error();
  }

  return zx::ok(std::move(driver));
}

// Initialize this driver instance
zx::status<> QemuEduDriver::Run(
    async_dispatcher* dispatcher,
    fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir) {

  FDF_SLOG(INFO, "edu driver loaded successfully");

  return zx::ok();
}

}  // namespace qemu_edu

// Register driver hooks with the framework
FUCHSIA_DRIVER_RECORD_CPP_V1(qemu_edu::QemuEduDriver);

```

This `Start()` method initializes an instance of the `QemuEduDriver` class and assigns it to the
driver variable. The function then calls the `Run()` method of the driver instance to perform
initialization tasks.

Add the following build rule to the bottom of your `qemu_edu/BUILD.bazel` file to compile the
driver code into a shared library binary:

*   `cc_binary()`: Specifies the source and header files (for instance, `qemu_edu.cc` and
    `qemu_edu.h`) for building a C++ binary.

`qemu_edu/BUILD.bazel`:

```bazel
cc_binary(
    name = "qemu_edu",
    srcs = [
        "qemu_edu.cc",
        "qemu_edu.h",
    ],
    linkshared = True,
    deps = [
        "@fuchsia_sdk//fidl/zx:zx_cc",
        "@fuchsia_sdk//pkg/driver2-llcpp",
        "@fuchsia_sdk//pkg/driver_runtime_cpp",
        "@fuchsia_sdk//pkg/fidl-llcpp",
        "@fuchsia_sdk//pkg/hwreg",
        "@fuchsia_sdk//pkg/mmio",
        "@fuchsia_sdk//pkg/sys_component_llcpp",
        "@fuchsia_sdk//pkg/zx",
    ],
)
```

## Load the driver

With the initial scaffolding in place, you can publish the driver to a local package repository
and verify that it binds successfully to the `edu` device.

Add the following rules to the bottom of your `qemu_edu/BUILD.bazel` file to build the driver
component into a Fuchsia package:

*   `fuchsia_driver_component()`: Describes the binaries and artifacts for the `qemu_edu` driver
    component.
*   `fuchsia_package()`: Builds the driver component into a [Fuchsia package][concepts-packages].

`qemu_edu/BUILD.bazel`:

```bazel
fuchsia_driver_component(
    name = "component",
    bind_bytecode = ":bind_bytecode",
    driver_lib = ":qemu_edu",
    manifest = ":manifest",
)

fuchsia_package(
    name = "pkg",
    package_name = "qemu_edu",
    visibility = ["//visibility:public"],
    deps = [
        ":component",
    ],
)
```

Use the `bazel run` command to build and execute the component target:

```posix-terminal
bazel run --config=fuchsia_x64 //fuchsia-codelab/qemu_edu:pkg.component
```

The `bazel run` command performs the following steps:

1.  Build the component and package
1.  Publish the package to a local package repository
1.  Register the package repository with the target device
1.  Use `ffx driver register` to load the driver component

You should see the driver framework automatically detect a match a bind the driver to the
`edu` device node:

```none {:.devsite-disable-click-to-copy}
INFO: Build completed successfully, 1 total action
added repository bazel.pkg.component
Registering fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm
Successfully bound:
Node 'root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_', Driver 'fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm'.
```

Inspect the system log and verify that you can see `FDF_SLOG()` message from the driver code after the driver successfully binds:

```posix-terminal
ffx log --filter qemu_edu
```

```none {:.devsite-disable-click-to-copy}
[driver_index][driver_index,driver][I] Registered driver successfully: fuchsia-pkg://fuchsiasamples.com/qemu_edu#meta/qemu_edu.cm.
[driver_manager][driver_manager.cm][I]: [driver_runner.cc:959] Binding fuchsia-pkg://fuchsiasamples.com/qemu_edu#meta/qemu_edu.cm to  00_06_0_
{{ '<strong>' }}[universe-pkg-drivers:root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_][qemu-edu,driver][I]: [fuchsia-codelab/qemu_edu/qemu_edu.cc:28] edu driver loaded successfully{{ '</strong>' }}
[driver-hosts:driver-host-3][][I]: [../../src/devices/bin/driver_host2/driver_host.cc:349] Started driver url=fuchsia-pkg://fuchsiasamples.com/qemu_edu#meta/qemu_edu.cm
```

## Explore the updated device node

Now that the driver has successfully bound to a device node, `ffx driver list` reports that your
driver is loaded:

```posix-terminal
ffx driver list --loaded
```

```none {:.devsite-disable-click-to-copy}
fuchsia-boot:///#meta/block.core.cm
fuchsia-boot:///#meta/bus-pci.cm
fuchsia-boot:///#meta/cpu-trace.cm
fuchsia-boot:///#meta/fvm.cm
fuchsia-boot:///#meta/hid.cm
fuchsia-boot:///#meta/intel-rtc.cm
fuchsia-boot:///#meta/netdevice-migration.cm
fuchsia-boot:///#meta/network-device.cm
fuchsia-boot:///#meta/pc-ps2.cm
fuchsia-boot:///#meta/platform-bus-x86.cm
fuchsia-boot:///#meta/platform-bus.cm
fuchsia-boot:///#meta/ramdisk.cm
fuchsia-boot:///#meta/sysmem.cm
fuchsia-boot:///#meta/virtio_block.cm
fuchsia-boot:///#meta/virtio_ethernet.cm
fuchsia-pkg://fuchsia.com/virtual_audio#meta/virtual_audio_driver.cm
{{ '<strong>' }}fuchsia-pkg://fuchsiasamples.com/qemu_edu#meta/qemu_edu.cm{{ '</strong>' }}
```

Inspect the device node once more using `ffx driver list-devices`, and verify that your driver is
now listed as attached to the `edu` device node:

```posix-terminal
ffx driver list-devices root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_ --verbose
```

```none {:.devsite-disable-click-to-copy}
Name     : 00_06_0_
Moniker  : root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_
{{ '<strong>' }}Driver   : fuchsia-pkg://fuchsiasamples.com/qemu_edu#meta/qemu_edu.cm{{ '</strong>' }}
11 Properties
[ 1/ 11] : Key fuchsia.BIND_FIDL_PROTOCOL     Value 0x000004
[ 2/ 11] : Key fuchsia.BIND_PCI_VID           Value 0x001234
[ 3/ 11] : Key fuchsia.BIND_PCI_DID           Value 0x0011e8
[ 4/ 11] : Key fuchsia.BIND_PCI_CLASS         Value 0x000000
[ 5/ 11] : Key fuchsia.BIND_PCI_SUBCLASS      Value 0x0000ff
[ 6/ 11] : Key fuchsia.BIND_PCI_INTERFACE     Value 0x000000
[ 7/ 11] : Key fuchsia.BIND_PCI_REVISION      Value 0x000010
[ 8/ 11] : Key fuchsia.BIND_PCI_TOPO          Value 0x000030
[ 9/ 11] : Key "fuchsia.hardware.pci.Device"  Value true
[10/ 11] : Key fuchsia.BIND_PROTOCOL          Value 0x000000
[11/ 11] : Key "fuchsia.driver.framework.dfv2" Value true
```

Congratulations! You have successfully bound a driver component to a device node on Fuchsia.

<!-- Reference links -->

[concepts-bind-libraries]: /docs/development/drivers/concepts/device_driver_model/driver-binding.md#bind-libraries
[concepts-component-manifest]: /docs/concepts/components/v2/component_manifests.md
[concepts-driver-binding]: /docs/concepts/drivers/driver_binding.md
[concepts-driver-manager]: /docs/concepts/drivers/driver_framework.md#driver_manager
[concepts-node-properties]: /docs/concepts/drivers/drivers_and_nodes.md#node_properties
[concepts-packages]: /docs/concepts/packages/package.md
