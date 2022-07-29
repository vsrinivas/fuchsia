# Configure hardware resources

Peripheral Component Interconnect (PCI) devices expose resources to the system using a variety of
interfaces including Interrupts, Memory-Mapped I/O (MMIO) registers, and Direct Memory Access (DMA)
buffers. Fuchsia drivers access these resources through capabilities from the parent device node.
For PCI devices, the parent offers an instance of the `fuchsia.hardware.pci/Device` FIDL protocol
to enable the driver to configure the device.

In this section, you'll be adding functionality to access the following MMIO registers on the `edu`
device:

Address offset | Register              | R/W | Description
-------------- | --------------------- | --- | -----------
0x00           | Identification        | RO  | Major / minor version identifier
0x04           | Card liveness check   | RW  | Challenge to verify operation
0x08           | Factorial computation | RW  | Compute factorial of the stored value
0x20           | Status                | RW  | Bitfields to signal the operation is complete

Note: For complete details on the `edu` device and its MMIO regions, see the
[device specification][edu-device-spec].

## Connect to the parent device

To access the `fuchsia.hardware.pci/Device` interface from the parent device node, add the
`fuchsia.driver.compat.Service` capability to the driver's component manifest:

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
{{ '<strong>' }}    use: [ {{ '</strong>' }}
{{ '<strong>' }}        { {{ '</strong>' }}
{{ '<strong>' }}            directory: 'fuchsia.driver.compat.Service-default', {{ '</strong>' }}
{{ '<strong>' }}            rights: ['rw*'], {{ '</strong>' }}
{{ '<strong>' }}            path: '/fuchsia.driver.compat.Service/default', {{ '</strong>' }}
{{ '<strong>' }}        }, {{ '</strong>' }}
{{ '<strong>' }}    ], {{ '</strong>' }}
}
```

This enables the driver to open a connection to the parent device and access the hardware-specific
protocols it offers. Add the following code to use the `fuchsia.driver.compat.Service` capability
to open the device connection:

`qemu_edu/qemu_edu.cc`:

```cpp
#include "fuchsia-codelab/qemu_edu/qemu_edu.h"

{{ '<strong>' }}#include <fidl/fuchsia.driver.compat/cpp/wire.h> {{ '</strong>' }}

{{ '<strong>' }}namespace { {{ '</strong>' }}

{{ '<strong>' }}// Connect to parent device node using fuchsia.driver.compat.Service {{ '</strong>' }}
{{ '<strong>' }}zx::status<fidl::ClientEnd<fuchsia_driver_compat::Device>> ConnectToParentDevice( {{ '</strong>' }}
{{ '<strong>' }}    const driver::Namespace* ns, std::string_view name) { {{ '</strong>' }}
{{ '<strong>' }}  auto result = ns->OpenService<fuchsia_driver_compat::Service>(name); {{ '</strong>' }}
{{ '<strong>' }}  if (result.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    return result.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  return result.value().connect_device(); {{ '</strong>' }}
{{ '<strong>' }}} {{ '</strong>' }}

{{ '<strong>' }}}  // namespace {{ '</strong>' }}
```

Update the driver's `Run()` method to access the `fuchsia.hardware.pci/Device` offered by the
parent device during driver initialization:

`qemu_edu/qemu_edu.cc`:

```cpp
// Initialize this driver instance
zx::status<> QemuEduDriver::Run(
    async_dispatcher* dispatcher,
    fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir) {

{{ '<strong>' }}  // Connect to the parent device node. {{ '</strong>' }}
{{ '<strong>' }}  auto parent = ConnectToParentDevice(&ns_, "default"); {{ '</strong>' }}
{{ '<strong>' }}  if (parent.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "Failed to connect to parent", {{ '</strong>' }}
{{ '<strong>' }}        KV("status", parent.status_string())); {{ '</strong>' }}
{{ '<strong>' }}    return parent.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  }

{{ '<strong>' }}  // Connect to fuchsia.hardware.pci FIDL protocol from the parent device {{ '</strong>' }}
{{ '<strong>' }}  auto pci_endpoints = fidl::CreateEndpoints<fuchsia_hardware_pci::Device>(); {{ '</strong>' }}
{{ '<strong>' }}  if (pci_endpoints.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    return pci_endpoints.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  auto connect_result = fidl::WireCall(*parent)->ConnectFidl( {{ '</strong>' }}
{{ '<strong>' }}      fidl::StringView::FromExternal {{ '</strong>' }}(fidl::DiscoverableProtocolName<fuchsia_hardware_pci::Device>), {{ '</strong>' }}
{{ '<strong>' }}      pci_endpoints->server.TakeChannel()); {{ '</strong>' }}
{{ '<strong>' }}  if (!connect_result.ok()) { {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(connect_result.status()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

  FDF_SLOG(INFO, "edu driver loaded successfully");

  return zx::ok();
}
```

## Set up interrupts and MMIO

With a connection open to the `fuchsia.hardware.pci/Device`, you can begin to map the necessary
device resources into the driver. Add the following code to your driver class to declare a new
`MapInterruptAndMmio()` method:

`qemu_edu/qemu_edu.h`:

```cpp
#include <lib/async/dispatcher.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/record_cpp.h>
#include <lib/driver2/structured_logger.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>
#include <lib/zx/status.h>

{{ '<strong>' }}#include <fidl/fuchsia.hardware.pci/cpp/wire.h> {{ '</strong>' }}
{{ '<strong>' }}#include <lib/mmio/mmio.h> {{ '</strong>' }}
{{ '<strong>' }}#include <lib/zx/interrupt.h> {{ '</strong>' }}

namespace qemu_edu {

class QemuEduDriver {
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

 private:
{{ '<strong>' }}  zx::status<> MapInterruptAndMmio(fidl::ClientEnd<fuchsia_hardware_pci::Device> pci); {{ '</strong>' }}
  zx::status<> Run(async_dispatcher* dispatcher,
                   fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir);
  
  component::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fuchsia_driver_framework::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;

{{ '<strong>' }}  std::optional<fdf::MmioBuffer> mmio_; {{ '</strong>' }}
{{ '<strong>' }}  zx::interrupt irq_; {{ '</strong>' }}
};

}  // namespace qemu_edu
```

This method performs the following tasks:

1.  Access the Base Address Register (BAR) of the appropriate PCI region.
1.  Extract Fuchsia's [VMO][concepts-kernel-vmo] (Virtual Memory Object) for the region.
1.  Create an MMIO buffer around the region to access individual registers.
1.  Configure an Interrupt Request (IRQ) mapped to the device's interrupt.

Add the following code to implement the `MapInterruptAndMmio()` method:

`qemu_edu/qemu_edu.cc`:

```cpp
namespace qemu_edu {
// ...

{{ '<strong>' }}// Initialize PCI device hardware resources {{ '</strong>' }}
{{ '<strong>' }}zx::status<> QemuEduDriver::MapInterruptAndMmio(fidl::ClientEnd<fuchsia_hardware_pci::Device> pci_client) { {{ '</strong>' }}
{{ '<strong>' }}  auto pci = fidl::BindSyncClient(std::move(pci_client)); {{ '</strong>' }}

{{ '<strong>' }}  // Retrieve the Base Address Register (BAR) for PCI Region 0 {{ '</strong>' }}
{{ '<strong>' }}  auto bar = pci->GetBar(0); {{ '</strong>' }}
{{ '<strong>' }}  if (!bar.ok()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "failed to get bar", KV("status", bar.status())); {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(bar.status()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  if (bar->is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "failed to get bar", KV("status", bar->error_value())); {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(bar->error_value()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  // Create a Memory-Mapped I/O (MMIO) region over BAR0 {{ '</strong>' }}
{{ '<strong>' }}  { {{ '</strong>' }}
{{ '<strong>' }}    auto& bar_result = bar->value()->result; {{ '</strong>' }}
{{ '<strong>' }}    if (!bar_result.result.is_vmo()) { {{ '</strong>' }}
{{ '<strong>' }}      FDF_SLOG(ERROR, "unexpected bar type"); {{ '</strong>' }}
{{ '<strong>' }}      return zx::error(ZX_ERR_NO_RESOURCES); {{ '</strong>' }}
{{ '<strong>' }}    } {{ '</strong>' }}
{{ '<strong>' }}    zx::status<fdf::MmioBuffer> mmio = fdf::MmioBuffer::Create( {{ '</strong>' }}
{{ '<strong>' }}        0, bar_result.size, std::move(bar_result.result.vmo()), ZX_CACHE_POLICY_UNCACHED_DEVICE); {{ '</strong>' }}
{{ '<strong>' }}    if (mmio.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}      FDF_SLOG(ERROR, "failed to map mmio", KV("status", mmio.status_value())); {{ '</strong>' }}
{{ '<strong>' }}      return mmio.take_error(); {{ '</strong>' }}
{{ '<strong>' }}    } {{ '</strong>' }}
{{ '<strong>' }}    mmio_ = *std::move(mmio); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  // Configure interrupt handling for the device {{ '</strong>' }}
{{ '<strong>' }}  auto result = pci->SetInterruptMode(fuchsia_hardware_pci::wire::InterruptMode::kLegacy, 1); {{ '</strong>' }}
{{ '<strong>' }}  if (!result.ok()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "failed configure interrupt mode", KV("status", result.status())); {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(result.status()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  if (result->is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "failed configure interrupt mode", KV("status", result->error_value())); {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(result->error_value()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

{{ '<strong>' }}  // Map the device's interrupt to a system IRQ {{ '</strong>' }}
{{ '<strong>' }}  auto interrupt = pci->MapInterrupt(0); {{ '</strong>' }}
{{ '<strong>' }}  if (!interrupt.ok()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "failed to map interrupt", KV("status", interrupt.status())); {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(interrupt.status()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  if (interrupt->is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    FDF_SLOG(ERROR, "failed to map interrupt", KV("status", interrupt->error_value())); {{ '</strong>' }}
{{ '<strong>' }}    return zx::error(interrupt->error_value()); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}
{{ '<strong>' }}  irq_ = std::move(interrupt->value()->interrupt); {{ '</strong>' }}

{{ '<strong>' }}  return zx::ok(); {{ '</strong>' }}
{{ '<strong>' }}} {{ '</strong>' }}

}  // namespace qemu_edu
```

Update the driver's `Run()` method to call the new method during driver initialization:

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

{{ '<strong>' }}  // Map hardware resources from the PCI device {{ '</strong>' }}
{{ '<strong>' }}  auto pci_status = MapInterruptAndMmio(std::move(pci_endpoints->client)); {{ '</strong>' }}
{{ '<strong>' }}  if (pci_status.is_error()) { {{ '</strong>' }}
{{ '<strong>' }}    return pci_status.take_error(); {{ '</strong>' }}
{{ '<strong>' }}  } {{ '</strong>' }}

  FDF_SLOG(INFO, "edu driver loaded successfully");

  return zx::ok();
}
```

Update the driver build configuration to depend on the FIDL binding libraries for these two
protocols:

`qemu_edu/BUILD.bazel`:

```bazel
cc_binary(
    name = "qemu_edu",
    srcs = [
        "qemu_edu.cc",
        "qemu_edu.h"
    ],
    linkshared = True,
    deps = [
{{ '<strong>' }}        "@fuchsia_sdk//fidl/fuchsia.driver.compat:fuchsia.driver.compat_llcpp_cc", {{ '</strong>' }}
{{ '<strong>' }}        "@fuchsia_sdk//fidl/fuchsia.hardware.pci:fuchsia.hardware.pci_llcpp_cc", {{ '</strong>' }}
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

## Read device registers

With the base resources mapped into the driver, you can access individual registers. Begin by
creating a new `qemu_edu/registers.h` file in your project directory with the following contents:

`qemu_edu/registers.h`:

```cpp
#ifndef FUCHSIA_CODELAB_QEMU_EDU_REGISTERS_H_
#define FUCHSIA_CODELAB_QEMU_EDU_REGISTERS_H_

#include <hwreg/bitfields.h>

namespace edu_device_registers {

// Register offset addresses for edu device MMIO area
constexpr uint32_t kIdentificationOffset = 0x00;
constexpr uint32_t kLivenessCheckOffset = 0x04;
constexpr uint32_t kFactorialCompoutationOffset = 0x08;
constexpr uint32_t kStatusRegisterOffset = 0x20;
constexpr uint32_t kInterruptStatusRegisterOffset = 0x24;
constexpr uint32_t kInterruptRaiseRegisterOffset = 0x60;
constexpr uint32_t kInterruptAcknowledgeRegisterOffset = 0x64;
constexpr uint32_t kDmaSourceAddressOffset = 0x80;
constexpr uint32_t kDmaDestinationAddressOffset = 0x80;
constexpr uint32_t kDmaTransferCountOffset = 0x90;
constexpr uint32_t kDmaCommandRegisterOffset = 0x98;

class Identification : public hwreg::RegisterBase<Identification, uint32_t> {
 public:
  DEF_FIELD(31, 24, major_version);
  DEF_FIELD(23, 16, minor_version);
  DEF_FIELD(15, 0, edu);
  static auto Get() { return hwreg::RegisterAddr<Identification>(kIdentificationOffset); }
};

class Status : public hwreg::RegisterBase<Status, uint32_t> {
 public:
  DEF_BIT(0, busy);
  DEF_BIT(7, irq_enable);
  static auto Get() { return hwreg::RegisterAddr<Status>(kStatusRegisterOffset); }
};

}  // namespace edu_device_registers

#endif  // FUCHSIA_CODELAB_QEMU_EDU_REGISTERS_H_

```

This file declares the register offsets provided in the device specification as constants.
Fuchsia's `hwreg` library wraps the registers that represent bitfields, making them easier to
access without performing individual bitwise operations.

Add the following to the driver's `Run()` method to read the major/minor version from the
identification register from the MMIO region and print it to the log:

`qemu_edu/qemu_edu.cc`:

```cpp
#include "fuchsia-codelab/qemu_edu/qemu_edu.h"

#include <fidl/fuchsia.driver.compat/cpp/wire.h>

{{ '<strong>' }}#include "fuchsia-codelab/qemu_edu/registers.h" {{ '</strong>' }}

// ...

// Initialize this driver instance
zx::status<> QemuEduDriver::Run(
    async_dispatcher* dispatcher,
    fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir) {

  // Connect to the parent device node.
  // ...

  // Connect to fuchsia.hardware.pci FIDL protocol from the parent device
  // ...

  // Map hardware resources from the PCI device
  auto pci_status = MapInterruptAndMmio(std::move(pci_endpoints->client));
  if (pci_status.is_error()) {
    return pci_status.take_error();
  }

{{ '<strong>' }}  // Report the version information from the edu device. {{ '</strong>' }}
{{ '<strong>' }}  auto version_reg = edu_device_registers::Identification::Get().ReadFrom(&*mmio_); {{ '</strong>' }}
{{ '<strong>' }}  FDF_SLOG(INFO, "edu device version", {{ '</strong>' }}
{{ '<strong>' }}      KV("major", version_reg.major_version()), {{ '</strong>' }}
{{ '<strong>' }}      KV("minor", version_reg.minor_version())); {{ '</strong>' }}

  return zx::ok();
}
```

Update the driver's build configuration to include `registers.h` as a source file:

`qemu_edu/BUILD.bazel`:

```bazel
cc_binary(
    name = "qemu_edu",
    srcs = [
        "qemu_edu.cc",
        "qemu_edu.h",
{{ '<strong>' }}        "registers.h", {{ '</strong>' }}
    ],
    linkshared = True,
    deps = [
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

Use the `bazel run` command to build and execute the component target:

```posix-terminal
bazel run --config=fuchsia_x64 //fuchsia-codelab/qemu_edu:pkg.component
```

The `bazel run` command rebuilds the package and runs `ffx driver register` to reload the
driver component.

Inspect the system log and verify that you can see the updated `FDF_SLOG()` message containing
the version read from the identification register:

```posix-terminal
ffx log --filter qemu_edu
```

```none {:.devsite-disable-click-to-copy}
[driver_manager][driver_manager.cm][I]: [driver_runner.cc:959] Binding fuchsia-pkg://fuchsiasamples.com/qemu_edu#meta/qemu_edu.cm to  00_06_0_
{{ '<strong>' }}[universe-pkg-drivers:root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_][qemu-edu,driver][I]: [fuchsia-codelab/qemu_edu/qemu_edu.cc:75] edu device version major=1 minor=0 {{ '</strong>' }}
```

Congratulations! Your driver can now access the PCI hardware resources provided by the bound
device node.

<!-- Reference links -->

[concepts-kernel-vmo]: /docs/concepts/kernel/concepts.md#shared_memory_virtual_memory_objects_vmos
[edu-device-spec]: https://fuchsia.googlesource.com/third_party/qemu/+/refs/heads/main/docs/specs/edu.txt
