# Expose the driver capabilities

Driver components offer the features and services they provide to other drivers and non-driver
components through [capabilities][concepts-capabilities]. This enables Fuchsia's component
framework to route those capabilities to the target component when necessary. Drivers can also
export their capabilities to [devfs][concepts-devfs] to enable other components to discover them
as file nodes mounted in the component's namespace.

In this section, you'll expose the `qemu_edu` driver's factorial capabilities and consume those
from a component running elsewhere in the system.

## Define a driver service protocol

The driver exposes the capabilities of the `edu` device using a custom FIDL protocol.
Add a new `qemu_edu/qemu_edu.fidl` file to your project workspace with the following contents:

`qemu_edu/qemu_edu.fidl`:

```fidl
library fuchsia.hardware.qemuedu;

protocol Device {
    // Computes the factorial of 'input' using the edu device and returns the
    // result.
    ComputeFactorial(struct {
        input uint32;
    }) -> (struct {
        output uint32;
    });

    // Performs a liveness check and return true if the device passes.
    LivenessCheck() -> (struct {
        result bool;
    });
};

```

This FIDL protocol provides two methods to interact with the factorial computation and
liveness check hardware registers on the `edu` device.

Add the following build rules to the bottom of the project's build configuration to compile the
FIDL library and generate C++ bindings:

*   `fuchsia_fidl_library()`: Declares the `fuchsia.hardware.qemuedu` FIDL library and describes
    the FIDL source files it includes.
*   `fuchsia_fidl_llcpp_library()`: Describes the generated
    [LLCPP (Low-Level C++) bindings][fidl-cpp-bindings] for components to interact with this
    FIDL library.

`qemu_edu/BUILD.bazel`:

```bazel
fuchsia_fidl_library(
    name = "fuchsia.hardware.qemuedu",
    srcs = [
        "qemu_edu.fidl",
    ],
    library = "fuchsia.hardware.qemuedu",
    visibility = ["//visibility:public"],
)

fuchsia_fidl_llcpp_library(
    name = "fuchsia.hardware.qemuedu_cc",
    library = ":fuchsia.hardware.qemuedu",
    visibility = ["//visibility:public"],
    deps = [
        "@fuchsia_sdk//fidl/zx:zx_llcpp_cc",
        "@fuchsia_sdk//pkg/fidl-llcpp",
    ],
)
```

## Implement the driver service protocol

With the FIDL protocol defined, you'll need to update the driver to implement and serve this
protocol to other components. Update the driver's component manifest to declare and expose the
protocol as a capability:

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
    use: [
        {
            directory: 'fuchsia.driver.compat.Service-default',
            rights: ['rw*'],
            path: '/fuchsia.driver.compat.Service/default',
        },
    ],
{{ '<strong>' }}    // Provide the device capability to other components {{ '</strong>' }}
{{ '<strong>' }}    capabilities: [ {{ '</strong>' }}
{{ '<strong>' }}        { protocol: 'fuchsia.hardware.qemuedu.Device' }, {{ '</strong>' }}
{{ '<strong>' }}    ], {{ '</strong>' }}
{{ '<strong>' }}    expose: [ {{ '</strong>' }}
{{ '<strong>' }}        { {{ '</strong>' }}
{{ '<strong>' }}            protocol: 'fuchsia.hardware.qemuedu.Device', {{ '</strong>' }}
{{ '<strong>' }}            from: 'self', {{ '</strong>' }}
{{ '<strong>' }}        }, {{ '</strong>' }}
{{ '<strong>' }}    ], {{ '</strong>' }}
}
```

Include the FIDL bindings for the `fuchsia.hardware.qemuedu` library and update the driver class
to implement the server end of the protocol:

`qemu_edu/qemu_edu.h`:

```cpp
#include <lib/async/dispatcher.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/record_cpp.h>
#include <lib/driver2/structured_logger.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>
#include <lib/zx/status.h>

#include <fidl/fuchsia.hardware.pci/cpp/wire.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>

{{ '<strong>' }}#include <fidl/fuchsia.hardware.qemuedu/cpp/wire.h> {{ '</strong>' }}

namespace qemu_edu {

{{ '<strong>' }}class QemuEduDriver : public fidl::WireServer<fuchsia_hardware_qemuedu::Device> { {{ '</strong>' }}
 public:
  QemuEduDriver(async_dispatcher_t* dispatcher,
                fidl::WireSharedClient<fuchsia_driver_framework::Node> node, driver::Namespace ns,
                driver::Logger logger)
      : outgoing_(component::OutgoingDirectory::Create(dispatcher)),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)) {}
  
  virtual ~QemuEduDriver() = default;

  // Report driver name to driver framework
  static constexpr const char* Name() { return "qemu-edu"; }
  // Start hook called by driver framework
  static zx::status<std::unique_ptr<QemuEduDriver>> Start(
      fuchsia_driver_framework::wire::DriverStartArgs& start_args,
      fdf::UnownedDispatcher dispatcher,
      fidl::WireSharedClient<fuchsia_driver_framework::Node> node, driver::Namespace ns,
      driver::Logger logger);

{{ '<strong>' }}  // fuchsia.hardware.qemuedu/Device protocol implementation. {{ '</strong>' }}
{{ '<strong>' }}  void ComputeFactorial(ComputeFactorialRequestView request, {{ '</strong>' }}
{{ '<strong>' }}                        ComputeFactorialCompleter::Sync& completer); {{ '</strong>' }}
{{ '<strong>' }}  void LivenessCheck(LivenessCheckRequestView request, LivenessCheckCompleter::Sync& completer); {{ '</strong>' }}

 private:
  zx::status<> MapInterruptAndMmio(fidl::ClientEnd<fuchsia_hardware_pci::Device> pci);
  zx::status<> Run(async_dispatcher* dispatcher,
                   fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir);
  
  component::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fuchsia_driver_framework::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;

  std::optional<fdf::MmioBuffer> mmio_;
  zx::interrupt irq_;
};

}  // namespace qemu_edu
```

Similar to the identification register in the previous section, these methods interact with the
MMIO region to read and write data into the respective `edu` device registers:

*   `ComputeFactorial()`: Write an input value to the factorial computation register and wait for
    the status register to report that the computation is complete.
*   `LivenessCheck()`: Write a challenge value to the liveness check register and confirm the
    expected result.

Add the following code to implement the `fuchsia.hardware.qemuedu/Device` method in your driver
class:

`qemu_edu/qemu_edu.cc`:

```cpp
namespace qemu_edu {
// ...

{{ '<strong>' }}// Driver Service: Compute factorial on the edu device {{ '</strong>' }}
{{ '<strong>' }}void QemuEduDriver::ComputeFactorial(ComputeFactorialRequestView request, {{ '</strong>' }}
{{ '<strong>' }}                                     ComputeFactorialCompleter::Sync& completer) { {{ '</strong>' }}
{{ '<strong>' }}  // Write a value into the factorial register. {{ '</strong>' }}
{{ '<strong>' }}  uint32_t input = request->input; {{ '</strong>' }}

{{ '<strong>' }}  mmio_->Write32(input, edu_device_registers::kFactorialCompoutationOffset); {{ '</strong>' }}

{{ '<strong>' }}  // Busy wait on the factorial status bit. {{ '</strong>' }}
{{ '<strong>' }}  while (true) { {{ '</strong>' }}
{{ '<strong>' }}    const auto status = edu_device_registers::Status::Get().ReadFrom(&*mmio_); {{ '</strong>' }}
{{ '<strong>' }}    if (!status.busy()) {{ '</strong>' }}
{{ '<strong>' }}      break; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  // Return the result. {{ '</strong>' }}
{{ '<strong>' }}  uint32_t factorial = mmio_->Read32(edu_device_registers::kFactorialCompoutationOffset); {{ '</strong>' }}

{{ '<strong>' }}  FDF_SLOG(INFO, "Replying with", KV("factorial", factorial)); {{ '</strong>' }}
{{ '<strong>' }}  completer.Reply(factorial); {{ '</strong>' }}
{{ '<strong>' }}} {{ '</strong>' }}

{{ '<strong>' }}// Driver Service: Complete a liveness check on the edu device {{ '</strong>' }}
{{ '<strong>' }}void QemuEduDriver::LivenessCheck(LivenessCheckRequestView request, {{ '</strong>' }}
{{ '<strong>' }}                                  LivenessCheckCompleter::Sync& completer) { {{ '</strong>' }}
{{ '<strong>' }}  constexpr uint32_t kChallenge = 0xdeadbeef; {{ '</strong>' }}
{{ '<strong>' }}  constexpr uint32_t kExpectedResponse = ~(kChallenge); {{ '</strong>' }}

{{ '<strong>' }}  // Write the challenge and observe that the expected response is received. {{ '</strong>' }}
{{ '<strong>' }}  mmio_->Write32(kChallenge, edu_device_registers::kLivenessCheckOffset); {{ '</strong>' }}
{{ '<strong>' }}  auto value = mmio_->Read32(edu_device_registers::kLivenessCheckOffset); {{ '</strong>' }}

{{ '<strong>' }}  const bool alive = value == kExpectedResponse; {{ '</strong>' }}

{{ '<strong>' }}  FDF_SLOG(INFO, "Replying with", KV("result", alive)); {{ '</strong>' }}
{{ '<strong>' }}  completer.Reply(alive); {{ '</strong>' }}
{{ '<strong>' }}} {{ '</strong>' }}

}  // namespace qemu_edu
```

Update the driver's build configuration to depend on the FIDL bindings for the new
`fuchsia.hardware.qemuedu` library:

`qemu_edu/BUILD.bazel`:

```bazel
cc_binary(
    name = "qemu_edu",
    srcs = [
        "qemu_edu.cc",
        "qemu_edu.h",
        "registers.h"
    ],
    linkshared = True,
    deps = [
{{ '<strong>' }}        ":fuchsia.hardware.qemuedu_cc", {{ '</strong>' }}
        "@fuchsia_sdk//fidl/fuchsia.driver.compat:fuchsia.driver.compat_llcpp_cc",
        "@fuchsia_sdk//fidl/fuchsia.hardware.pci:fuchsia.hardware.pci_llcpp_cc",
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

## Export and serve the protocol

The `qemu_edu` driver makes the `fuchsia.hardware.qemuedu/Device` protocol discoverable to other
components using devfs. To discover which driver services are available in the system, a non-driver
component would look up the device filesystem (usually mounted to `/dev` in a component’s namespace)
and scan for the directories and files under this filesystem.

Driver manager can alias entries in devfs to a specific **device class** entry (for example,
`/dev/class/input`) when a matching **protocol ID** to a known device class is provided. If a
non-driver component does not know the exact path of the driver service in devfs, but rather a
specific type. For this exercise, the `edu` device does not conform to a known class so you will
configure this entry as an **unclassified device**.

Update the driver component's manifest to request the `fuchsia.devics.fs.Exporter` capability:

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
    use: [
{{ '<strong>' }}        { {{ '</strong>' }}
{{ '<strong>' }}            protocol: [ 'fuchsia.device.fs.Exporter' ], {{ '</strong>' }}
{{ '<strong>' }}        }, {{ '</strong>' }}
        {
            directory: 'fuchsia.driver.compat.Service-default',
            rights: ['rw*'],
            path: '/fuchsia.driver.compat.Service/default',
        },
    ],
    // Provide the device capability to other components
    capabilities: [
        { protocol: 'fuchsia.hardware.qemuedu.Device' },
    ],
    expose: [
        {
            protocol: 'fuchsia.hardware.qemuedu.Device',
            from: 'self',
        },
    ],
}
```

Add the following code to use the `fuchsia.device.fs.Exporter` capability to create a devfs entry:

`qemu_edu/qemu_edu.cc`:

```cpp
#include "fuchsia-codelab/qemu_edu/qemu_edu.h"

{{ '<strong>' }}#include <fidl/fuchsia.device.fs/cpp/wire.h> {{ '</strong>' }}
#include <fidl/fuchsia.driver.compat/cpp/wire.h>

#include "fuchsia-codelab/qemu_edu/registers.h"

namespace {

// Connect to parent device node using fuchsia.driver.compat.Service
zx::status<fidl::ClientEnd<fuchsia_driver_compat::Device>> ConnectToParentDevice(
    const driver::Namespace* ns, std::string_view name) {
  auto result = ns->OpenService<fuchsia_driver_compat::Service>(name);
  if (result.is_error()) {
    return result.take_error();
  }
  return result.value().connect_device();
}

{{ '<strong>' }}// Connect to the fuchsia.devices.fs.Exporter protocol {{ '</strong>' }}
{{ '<strong>' }}zx::status<fidl::ClientEnd<fuchsia_device_fs::Exporter>> ConnectToDeviceExporter( {{ '</strong>' }}
{{ '<strong>' }}    const driver::Namespace* ns) { {{ '</strong>' }}
{{ '<strong>' }}  auto exporter = ns->Connect<fuchsia_device_fs::Exporter>(); {{ '</strong>' }}
{{ '<strong>' }}  if (exporter.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    return exporter.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  return exporter; {{ '</strong>' }}
{{ '<strong>' }}} {{ '</strong>' }}

{{ '<strong>' }}// Create an exported directory handle using fuchsia.devices.fs.Exporter {{ '</strong>' }}
{{ '<strong>' }}zx::status<fidl::ServerEnd<fuchsia_io::Directory>> ExportDevfsEntry(const driver::Namespace* ns, {{ '</strong>' }}
{{ '<strong>' }}    fidl::StringView service_dir, fidl::StringView devfs_path, int protocol_id) { {{ '</strong>' }}
{{ '<strong>' }}  // Connect to the devfs exporter service {{ '</strong>' }}
{{ '<strong>' }}  auto exporter_client = ConnectToDeviceExporter(ns); {{ '</strong>' }}
{{ '<strong>' }}  if (exporter_client.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    return exporter_client.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  auto exporter = fidl::BindSyncClient(std::move(exporter_client.value())); {{ '</strong>' }}

{{ '<strong>' }}  // Serve a connection for devfs clients {{ '</strong>' }}
{{ '<strong>' }}  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>(); {{ '</strong>' }}
{{ '<strong>' }}  if (endpoints.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    return endpoints.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  // Export the client side of the service connection to devfs {{ '</strong>' }}
{{ '<strong>' }}  auto result = exporter->Export(std::move(endpoints->client), {{ '</strong>' }}
{{ '<strong>' }}      service_dir, devfs_path, protocol_id); {{ '</strong>' }}
{{ '<strong>' }}  if (!result.ok()) { {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(result.status()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  if (result->is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(result->error_value()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  return zx::ok(std::move(endpoints->server)); {{ '</strong>' }}
{{ '<strong>' }}} {{ '</strong>' }}

}  // namespace
```

Update the driver's `Run()` method to begin serving the `fuchsia.hardware.qemuedu/Device` protocol
to a new devfs entry at `/dev/sys/platform/platform-passthrough/PCI0/bus/00:06.0_/qemu-edu`, which
matches the device node's topological path:

`qemu_edu/qemu_edu.cc`:

```cpp
// Initialize this driver instance
zx::status<> QemuEduDriver::Run(
    async_dispatcher* dispatcher,
    fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir) {

  // Connect to the parent device node.
  // ...

  // Connect to fuchsia.hardware.pci FIDL protocol from the parent device
  // ...

  // Map hardware resources from the PCI device
  // ...

  // Report the version information from the edu device.
  auto version_reg = edu_device_registers::Identification::Get().ReadFrom(&*mmio_);
  FDF_SLOG(INFO, "edu device version",
      KV("major", version_reg.major_version()),
      KV("minor", version_reg.minor_version()));

{{ '<strong>' }}  // Add the fuchsia.hardware.qemuedu protocol to be served as "/svc/qemu-edu" {{ '</strong>' }}
{{ '<strong>' }}  auto status = outgoing_.AddProtocol<fuchsia_hardware_qemuedu::Device>(this, Name()); {{ '</strong>' }}
{{ '<strong>' }}  if (status.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "Failed to add protocol", KV("status", status.status_string())); {{ '</strong>' }}
{{ '<strong>' }}    return status; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  auto service_dir = fidl::StringView::FromExternal(std::string("svc/").append(Name())); {{ '</strong>' }}

{{ '<strong>' }}  // Construct a devfs path that matches the device nodes topological path {{ '</strong>' }}
{{ '<strong>' }}  auto path_result = fidl::WireCall(*parent)->GetTopologicalPath(); {{ '</strong>' }}
{{ '<strong>' }}  if (!path_result.ok()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "Failed to get topological path", KV("status", path_result.status_string())); {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(path_result.status()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  std::string parent_path(path_result->path.get()); {{ '</strong>' }}
{{ '<strong>' }}  auto devfs_path = fidl::StringView::FromExternal(parent_path.append("/").append(Name())); {{ '</strong>' }}
  
{{ '<strong>' }}  // Export an entry to devfs for fuchsia.hardware.qemuedu as an {{ '</strong>' }}
{{ '<strong>' }}  // Unclassified PCI device (protocol ID 0) {{ '</strong>' }}
{{ '<strong>' }}  auto devfs_dir = ExportDevfsEntry(&ns_, service_dir, devfs_path, 0); {{ '</strong>' }}
{{ '<strong>' }}  if (devfs_dir.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "Failed to export service", KV("status", devfs_dir.status_string())); {{ '</strong>' }}
{{ '<strong>' }}    return devfs_dir.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  // Serve the driver's FIDL protocol to clients {{ '</strong>' }}
{{ '<strong>' }}  status = outgoing_.Serve(std::move(devfs_dir.value())); {{ '</strong>' }}
{{ '<strong>' }}  if (status.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "Failed to serve devfs directory", KV("status", status.status_string())); {{ '</strong>' }}
{{ '<strong>' }}    return status.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  status = outgoing_.Serve(std::move(outgoing_dir)); {{ '</strong>' }}
{{ '<strong>' }}  if (status.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "Failed to serve outgoing directory", KV("status", status.status_string())); {{ '</strong>' }}
{{ '<strong>' }}   return status.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

  return zx::ok();
}
```

Update the driver's build configuration to include the FIDL bindings for the `fuchsia.device.fs`
library:

`qemu_edu/BUILD.bazel`:

```bazel
cc_binary(
    name = "qemu_edu",
    srcs = [
        "qemu_edu.cc",
        "qemu_edu.h",
        "registers.h"
    ],
    linkshared = True,
    deps = [
        ":fuchsia.hardware.qemuedu_cc",
{{ '<strong>' }}        "@fuchsia_sdk//fidl/fuchsia.device.fs:fuchsia.device.fs_llcpp_cc", {{ '</strong>' }}
        "@fuchsia_sdk//fidl/fuchsia.driver.compat:fuchsia.driver.compat_llcpp_cc",
        "@fuchsia_sdk//fidl/fuchsia.hardware.pci:fuchsia.hardware.pci_llcpp_cc",
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

The `qemu_edu` driver's capabilities are now discoverable by other components.

## Create a new tools component

In this section, you'll create a new `eductl` component that discovers and interacts with the
capabilities exposed by the `qemu_edu` driver.

After you complete this section, the project should have the following directory structure:

```none {:.devsite-disable-click-to-copy}
//fuchsia-codelab/qemu_edu
                  |- BUILD.bazel
                  |- meta
{{ '<strong>' }}                  |   |- eductl.cml {{ '</strong>' }}
                  |   |- qemu_edu.cml
{{ '<strong>' }}                  |- eductl.cc {{ '</strong>' }}
                  |- qemu_edu.bind
                  |- qemu_edu.cc
                  |- qemu_edu.fidl
                  |- qemu_edu.h
                  |- registers.h
```

Create a new `qemu_edu/meta/eductl.cml` component manifest file to the project with the following
contents:

`qemu_edu/meta/eductl.cml`:

```json5
{
    include: [
        "syslog/elf_stdio.shard.cml",
    ],
    program: {
        runner: 'elf',
        binary: 'bin/eductl',
        args: [
          'factorial',
          '12',
        ]
    },
    use: [
        {
            directory: "dev",
            rights: [ "rw*" ],
            path: "/dev",
        },
    ],
}

```

This component requests the `dev` directory capability, which enables it to discover and access
entries in devfs. Create a new `qemu_edu/eductl.cc` file with the following code to set up a basic
command line executable:

`qemu_edu/eductl.cc`:

```cpp
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

int usage(const char* cmd) {
  fprintf(stderr,
          "\nInteract with the QEMU edu device:\n"
          "   %s live                       Performs a card liveness check\n"
          "   %s fact <n>                   Computes the factorial of n\n"
          "   %s help                       Print this message\n",
          cmd, cmd, cmd);
  return -1;
}

// Returns "true" if the argument matches the prefix.
// In this case, moves the argument past the prefix.
bool prefix_match(const char** arg, const char* prefix) {
  if (!strncmp(*arg, prefix, strlen(prefix))) {
    *arg += strlen(prefix);
    return true;
  }
  return false;
}

constexpr long kBadParse = -1;
long parse_positive_long(const char* number) {
  char* end;
  long result = strtol(number, &end, 10);
  if (end == number || *end != '\0' || result < 0) {
    return kBadParse;
  }
  return result;
}


int main(int argc, char* argv[]) {
  const char* cmd = basename(argv[0]);

  // If no arguments passed, bail out after dumping
  // usage information.
  if (argc < 2) {
    return usage(cmd);
  }
  
  const char* arg = argv[1];
  if (prefix_match(&arg, "live")) {
    // TODO: liveness check
  } else if (prefix_match(&arg, "fact")) {
    // TODO: compute factorial
  }
  
  return usage(cmd);
}

```

This executable supports two subcommands to execute the liveness check and factorial computation.

Add the following new rules to the bottom of the project's build configuration to build this new
component into a Fuchsia package:

`qemu_edu/BUILD.bazel`:

```bazel
fuchsia_cc_binary(
    name = "eductl",
    srcs = [
        "eductl.cc",
    ],
    deps = [
        "@fuchsia_sdk//pkg/fdio",
        "@fuchsia_sdk//pkg/fidl-llcpp",
    ],
)

fuchsia_component_manifest(
    name = "eductl_manifest",
    src = "meta/eductl.cml",
    includes = [
        "@fuchsia_sdk//pkg/syslog:client",
        "@fuchsia_sdk//pkg/syslog:elf_stdio",
    ],
)

fuchsia_component(
    name = "eductl_component",
    manifest = ":eductl_manifest",
    deps = [
        ":eductl",
    ],
)

fuchsia_package(
    name = "eductl_pkg",
    package_name = "eductl",
    visibility = ["//visibility:public"],
    deps = [
        ":eductl_component",
    ],
)
```

## Implement the client tool

When client components open a connection to an entry in devfs, they receive an instance of the
FIDL protocol being served by the driver. Add the following code to the tools component to open a
connection to the `edu` device using its devfs path:

`qemu_edu/eductl.cc`:

```cpp
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

{{ '<strong>' }}#include <fidl/fuchsia.hardware.qemuedu/cpp/wire.h> {{ '</strong>' }}

{{ '<strong>' }}constexpr char kEduDevicePath[] = {{ '</strong>' }}
{{ '<strong>' }}    "/dev/sys/platform/platform-passthrough/PCI0/bus/00:06.0_/qemu-edu"; {{ '</strong>' }}

{{ '<strong>' }}// Open a FIDL client connection to the fuchsia.hardware.qemuedu.Device {{ '</strong>' }}
{{ '<strong>' }}fidl::WireSyncClient<fuchsia_hardware_qemuedu::Device> OpenDevice() { {{ '</strong>' }}
{{ '<strong>' }}  int device = open(kEduDevicePath, O_RDWR); {{ '</strong>' }}

{{ '<strong>' }}  if (device < 0) { {{ '</strong>' }}
{{ '<strong>' }}    fprintf(stderr, "Failed to open qemu edu device: %s\n", strerror(errno)); {{ '</strong>' }}
{{ '<strong>' }}    return {}; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  fidl::ClientEnd<fuchsia_hardware_qemuedu::Device> client_end; {{ '</strong>' }}
{{ '<strong>' }}  zx_status_t st = fdio_get_service_handle(device, client_end.channel().reset_and_get_address()); {{ '</strong>' }}
{{ '<strong>' }}  if (st != ZX_OK) { {{ '</strong>' }}
{{ '<strong>' }}    fprintf(stderr, "Failed to get service handle: %s\n", zx_status_get_string(st)); {{ '</strong>' }}
{{ '<strong>' }}    return {}; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  return fidl::BindSyncClient(std::move(client_end)); {{ '</strong>' }}
{{ '<strong>' }}} {{ '</strong>' }}

// ...
```

Add `liveness_check()` and `compute_factorial()` functions to call methods using the
`fuchsia.hardware.qemuedu/Device` FIDL protocol returned from `OpenDevice()`.
Finally, update the tool's `main()` function to call the appropriate device function based on
the argument passed on the command line:

`qemu_edu/eductl.cc`:

```cpp
// ...

{{ '<strong>' }}// Run a liveness check on the QEMU edu device. {{ '</strong>' }}
{{ '<strong>' }}// Returns 0 on success. {{ '</strong>' }}
{{ '<strong>' }}int liveness_check() { {{ '</strong>' }}
{{ '<strong>' }}  auto client = OpenDevice(); {{ '</strong>' }}
{{ '<strong>' }}  if (!client.is_valid()) { {{ '</strong>' }}
{{ '<strong>' }}    return -1; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  auto liveness_check_result = client->LivenessCheck(); {{ '</strong>' }}
{{ '<strong>' }}  if (!liveness_check_result.ok()) { {{ '</strong>' }}
{{ '<strong>' }}    fprintf(stderr, "Error: failed to get liveness check result: %s\n", {{ '</strong>' }}
{{ '<strong>' }}            zx_status_get_string(liveness_check_result.status())); {{ '</strong>' }}
{{ '<strong>' }}    return -1; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  if (liveness_check_result->result) { {{ '</strong>' }}
{{ '<strong>' }}    printf("Liveness check passed!\n"); {{ '</strong>' }}
{{ '<strong>' }}    return 0; {{ '</strong>' }}
{{ '<strong>' }}  } else { {{ '</strong>' }}
{{ '<strong>' }}    printf("Liveness check failed!\n"); {{ '</strong>' }}
{{ '<strong>' }}    return -1; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}} {{ '</strong>' }}

{{ '<strong>' }}// Compute the factorial of n using the QEMU edu device. {{ '</strong>' }}
{{ '<strong>' }}// Returns 0 on success. {{ '</strong>' }}
{{ '<strong>' }}int compute_factorial(long n) { {{ '</strong>' }}
{{ '<strong>' }}  auto client = OpenDevice(); {{ '</strong>' }}
{{ '<strong>' }}  if (!client.is_valid()) { {{ '</strong>' }}
{{ '<strong>' }}    return -1; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  if (n >= std::numeric_limits<uint32_t>::max()) { {{ '</strong>' }}
{{ '<strong>' }}    fprintf(stderr, "N is too large\n"); {{ '</strong>' }}
{{ '<strong>' }}    return -1; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  uint32_t input = static_cast<uint32_t>(n); {{ '</strong>' }}
{{ '<strong>' }}  auto compute_factorial_result = client->ComputeFactorial(input); {{ '</strong>' }}
{{ '<strong>' }}  if (!compute_factorial_result.ok()) { {{ '</strong>' }}
{{ '<strong>' }}    fprintf(stderr, "Error: failed to call compute factorial result: %s\n", {{ '</strong>' }}
{{ '<strong>' }}            zx_status_get_string(compute_factorial_result.status())); {{ '</strong>' }}
{{ '<strong>' }}    return -1; {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  printf("Factorial(%u) = %u\n", input, compute_factorial_result->output); {{ '</strong>' }}

{{ '<strong>' }}  return 0; {{ '</strong>' }}
{{ '<strong>' }}} {{ '</strong>' }}

int main(int argc, char* argv[]) {
  const char* cmd = basename(argv[0]);

  // If no arguments passed, bail out after dumping
  // usage information.
  if (argc < 2) {
    return usage(cmd);
  }
  
  const char* arg = argv[1];
  if (prefix_match(&arg, "live")) {
{{ '<strong>' }}    return liveness_check(); {{ '</strong>' }}
  } else if (prefix_match(&arg, "fact")) {
{{ '<strong>' }}    if (argc < 3) { {{ '</strong>' }}
{{ '<strong>' }}      fprintf(stderr, "Expecting 1 argument\n"); {{ '</strong>' }}
{{ '<strong>' }}      return usage(cmd); {{ '</strong>' }}
{{ '<strong>' }}    } {{ '</strong>' }}
{{ '<strong>' }}    long n = parse_positive_long(argv[2]); {{ '</strong>' }}
{{ '<strong>' }}    return compute_factorial(n); {{ '</strong>' }}
  }
  
  return usage(cmd);
}
```

Update the tools component's build configuration to depend on the FIDL bindings for the
`fuchsia.hardware.qemuedu` library:

`qemu_edu/BUILD.bazel`:

```bazel
fuchsia_cc_binary(
    name = "eductl",
    srcs = [
        "eductl.cc",
    ],
    deps = [
{{ '<strong>' }}        ":fuchsia.hardware.qemuedu_cc", {{ '</strong>' }}
        "@fuchsia_sdk//pkg/fdio",
        "@fuchsia_sdk//pkg/fidl-llcpp",
    ],
)
```

## Restart the emulator

Shut down any existing emulator instances:

```posix-terminal
ffx emu stop --all
```

Start a new instance of the Fuchsia emulator with driver framework enabled:

```posix-terminal
ffx emu start workstation.qemu-x64 --headless \
 --kernel-args "driver_manager.use_driver_framework_v2=true" \
 --kernel-args "driver_manager.root-driver=fuchsia-boot:///#meta/platform-bus.cm" \
 --kernel-args "devmgr.enable-ephemeral=true"
```

## Reload the driver

Use the `bazel run` command to build and execute the driver component target:

```posix-terminal
bazel run --config=fuchsia_x64 //fuchsia-codelab/qemu_edu:pkg.component
```

## Run the tool

Use the `bazel run` command to build and execute the tools component target:

```posix-terminal
bazel run --config=fuchsia_x64 //fuchsia-codelab/qemu_edu:eductl_pkg.eductl_component
```

The `bazel run` command performs the following steps:

1.  Build the component and package
1.  Publish the package to a local package repository
1.  Register the package repository with the target device
1.  Use `ffx component run --recreate` to run the component inside the
    [`ffx-laboratory`][ffx-laboratory].

Inspect the system log and verify that you can see the driver responding to a request from the
`eductl` component, followed by the tool printing the result:

```posix-terminal
ffx log --filter qemu_edu --filter eductl
```

```none {:.devsite-disable-click-to-copy}
[universe-pkg-drivers:root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_][qemu-edu,driver][I]: [fuchsia-codelab/qemu_edu/qemu_edu.cc:232] Replying with factorial=479001600
[ffx-laboratory:eductl][][I] Factorial(12) = 479001600
```

Congratulations! You've successfully exposed FIDL services from a Fuchsia driver and consumed them in a separate client component.

<!-- Reference links -->

[concepts-capabilities]: /docs/concepts/components/v2/capabilities/README.md
[concepts-devfs]: /docs/concepts/drivers/driver_communication.md#service_discovery_using_devfs
[fidl-cpp-bindings]: /docs/development/languages/fidl/guides/c-family-comparison.md
[ffx-laboratory]: /docs/development/components/run.md#ffx-laboratory
