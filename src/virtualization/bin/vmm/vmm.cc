// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/vmm.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include <atomic>
#include <unordered_map>
#include <vector>

#include "src/virtualization/bin/vmm/linux.h"
#include "src/virtualization/bin/vmm/pci.h"
#include "src/virtualization/bin/vmm/platform_device.h"
#include "src/virtualization/bin/vmm/uart.h"
#include "src/virtualization/bin/vmm/zircon.h"

namespace vmm {

using ::fuchsia::virtualization::BalloonController;
using ::fuchsia::virtualization::GuestConfig;
using ::fuchsia::virtualization::GuestError;
using ::fuchsia::virtualization::HostVsockEndpoint;
using ::fuchsia::virtualization::KernelType;

namespace {

bool IsValidConfig(const ::fuchsia::virtualization::GuestConfig& guest_config) {
  if (!guest_config.has_guest_memory()) {
    FX_LOGS(ERROR) << "Config must set the amount of required guest memory";
    return false;
  }

  if (!guest_config.has_cpus()) {
    FX_LOGS(ERROR) << "Config must set the number of cpus";
    return false;
  }

  if (!guest_config.has_kernel_type()) {
    FX_LOGS(ERROR) << "Config must set a kernel type";
    return false;
  }

  switch (guest_config.kernel_type()) {
    case KernelType::ZIRCON:
    case KernelType::LINUX:
      break;
    default:
      FX_LOGS(ERROR) << "Unknown kernel: " << static_cast<int64_t>(guest_config.kernel_type());
      return false;
  }

  return true;
}

// Duplicates a socket, asserting on failure.
zx::socket DuplicateSocket(const zx::socket& socket) {
  zx::socket duplicate_socket;
  FX_CHECK(socket.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_socket) == ZX_OK);
  return duplicate_socket;
}

}  // namespace

Vmm::~Vmm() {
  // Explicitly destroy the guest in the destructor to ensure it's the first object destroyed. The
  // guest has ownership of VCPU threads that may attempt to access various other objects via the
  // guest, and its destructor joins those threads avoiding any use after free problems.
  guest_.reset();
  if (outgoing_) {
    outgoing_->RemovePublicService<Guest>();
  }
}

fit::result<GuestError> Vmm::Initialize(GuestConfig cfg, ::sys::ComponentContext* context,
                                        async_dispatcher_t* dispatcher) {
  if (!IsValidConfig(cfg)) {
    return fit::error(GuestError::BAD_CONFIG);
  }

  outgoing_ = context->outgoing();

  DevMem dev_mem;
  guest_ = std::make_unique<::Guest>();
  zx_status_t status = guest_->Init(cfg.guest_memory());
  if (status != ZX_OK) {
    return fit::error(GuestError::GUEST_INITIALIZATION_FAILURE);
  }

  // Setup interrupt controller.
  interrupt_controller_ = std::make_unique<InterruptController>(guest_.get());
#if __aarch64__
  status = interrupt_controller_->Init(cfg.cpus());
#elif __x86_64__
  status = interrupt_controller_->Init();
#else
#error Unknown architecture.
#endif
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create interrupt controller";
    return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
  }
  platform_devices_.push_back(interrupt_controller_.get());

  // Setup UARTs.
  zx::socket host_serial_socket;
  FX_CHECK(zx::socket::create(0, &host_serial_socket, &client_serial_socket_) == ZX_OK);
  uart_ = std::make_unique<Uart>(std::move(host_serial_socket));
#if __aarch64__
  status = uart_->Init(guest_.get());
#elif __x86_64__
  status =
      uart_->Init(guest_.get(), [this](uint32_t irq) { interrupt_controller_->Interrupt(irq); });
#else
#error Unknown architecture.
#endif
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create UART";
    return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
  }
  platform_devices_.push_back(uart_.get());

#if __aarch64__
  // Setup PL031 RTC.
  pl031_ = std::make_unique<Pl031>();
  status = pl031_->Init(guest_.get());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create PL031 RTC";
    return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
  }
  platform_devices_.push_back(pl031_.get());
#elif __x86_64__
  // Setup IO ports.
  io_port_ = std::make_unique<IoPort>();
  status = io_port_->Init(guest_.get());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create IO ports";
    return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
  }
#else
#error Unknown architecture.
#endif

  // Setup PCI.
  pci_bus_ = std::make_unique<PciBus>(guest_.get(), interrupt_controller_.get());
  status = pci_bus_->Init(dispatcher);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create PCI bus";
    return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
  }
  platform_devices_.push_back(pci_bus_.get());

  // Setup balloon device.
  if (cfg.has_virtio_balloon() && cfg.virtio_balloon()) {
    balloon_ = std::make_unique<VirtioBalloon>(guest_->phys_mem());
    status = pci_bus_->Connect(balloon_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect balloon device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = balloon_->Start(guest_->object(), context, dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start balloon device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
  }

  // Create a new VirtioBlock device for each device requested.
  for (auto& block_device : *cfg.mutable_block_devices()) {
    auto block =
        std::make_unique<VirtioBlock>(guest_->phys_mem(), block_device.mode, block_device.format);
    status = pci_bus_->Connect(block->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect block device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = block->Start(guest_->object(), block_device.id, std::move(block_device.client),
                          context, dispatcher, block_devices_.size());

    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start block device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
    block_devices_.push_back(std::move(block));
  }

  // Setup console device.
  if (cfg.has_virtio_console() && cfg.virtio_console()) {
    console_ = std::make_unique<VirtioConsole>(guest_->phys_mem());
    status = pci_bus_->Connect(console_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect console device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }

    zx::socket host_console_socket;
    FX_CHECK(zx::socket::create(0, &host_console_socket, &client_console_socket_) == ZX_OK);
    status = console_->Start(guest_->object(), std::move(host_console_socket), context, dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start console device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
  }

  if (cfg.has_virtio_gpu() && cfg.virtio_gpu()) {
    gpu_ = std::make_unique<VirtioGpu>(guest_->phys_mem());
    input_keyboard_ = std::make_unique<VirtioInput>(guest_->phys_mem(), VirtioInput::Keyboard);
    input_pointer_ = std::make_unique<VirtioInput>(guest_->phys_mem(), VirtioInput::Pointer);

    // Setup keyboard device.
    status = pci_bus_->Connect(input_keyboard_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect keyboard device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = input_keyboard_->Start(guest_->object(), context, dispatcher, "virtio_input_keyboard");
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start keyboard device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::KeyboardListener> keyboard_listener;
    input_keyboard_->Connect(keyboard_listener.NewRequest());

    // Setup pointer device.
    status = pci_bus_->Connect(input_pointer_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect mouse device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = input_pointer_->Start(guest_->object(), context, dispatcher, "virtio_input_pointer");
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start mouse device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::PointerListener> pointer_listener;
    input_pointer_->Connect(pointer_listener.NewRequest());

    // Setup GPU device.
    status = pci_bus_->Connect(gpu_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect GPU device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = gpu_->Start(guest_->object(), std::move(keyboard_listener),
                         std::move(pointer_listener), context, dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start GPU device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
  }

  // Setup RNG device.
  if (cfg.has_virtio_rng() && cfg.virtio_rng()) {
    rng_ = std::make_unique<VirtioRng>(guest_->phys_mem());
    status = pci_bus_->Connect(rng_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect RNG device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = rng_->Start(guest_->object(), context, dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start RNG device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
  }

  if (cfg.has_virtio_vsock() && cfg.virtio_vsock()) {
    vsock_ = std::make_unique<controller::VirtioVsock>(guest_->phys_mem());
    status = pci_bus_->Connect(vsock_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect vsock device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = vsock_->Start(guest_->object(), std::move(*cfg.mutable_vsock_listeners()), context,
                           dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start vsock device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
  }

  // Setup wayland device.
  if (cfg.has_wayland_device()) {
    wl_ = std::make_unique<VirtioWl>(guest_->phys_mem());
    const size_t wl_dev_mem_size = cfg.wayland_device().memory;
    const zx_gpaddr_t wl_dev_mem_offset = AllocDeviceAddr(wl_dev_mem_size);
    if (!dev_mem.AddRange(wl_dev_mem_offset, wl_dev_mem_size)) {
      FX_LOGS(INFO) << "Could not reserve device memory range for wayland device";
      return fit::error(GuestError::INTERNAL_ERROR);
    }
    zx::vmar wl_vmar;
    status = guest_->CreateSubVmar(wl_dev_mem_offset, wl_dev_mem_size, &wl_vmar);
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not create VMAR for wayland device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = pci_bus_->Connect(wl_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not connect wayland device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem_allocator = nullptr;
    status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                  sysmem_allocator.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not connect to sysmem allocator service";
      return fit::error(GuestError::FAILED_SERVICE_CONNECT);
    }
    fidl::InterfaceHandle<fuchsia::ui::composition::Allocator> scenic_allocator = nullptr;
    status = fdio_service_connect("/svc/fuchsia.ui.composition.Allocator",
                                  scenic_allocator.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not connect to scenic allocator service";
      return fit::error(GuestError::FAILED_SERVICE_CONNECT);
    }
    status = wl_->Start(
        guest_->object(), std::move(wl_vmar), std::move(cfg.mutable_wayland_device()->server),
        std::move(sysmem_allocator), std::move(scenic_allocator), context, dispatcher);
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Could not start wayland device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
  }

  // Setup magma device.
  if (cfg.has_magma_device()) {
    magma_ = std::make_unique<VirtioMagma>(guest_->phys_mem());
    // TODO(fxbug.dev/12619): simplify vmm launch configs
    const size_t magma_dev_mem_size = cfg.magma_device().memory;
    const zx_gpaddr_t magma_dev_mem_offset = AllocDeviceAddr(magma_dev_mem_size);
    if (!dev_mem.AddRange(magma_dev_mem_offset, magma_dev_mem_size)) {
      FX_PLOGS(INFO, status) << "Could not reserve device memory range for magma device";
      return fit::error(GuestError::INTERNAL_ERROR);
    }
    zx::vmar magma_vmar;
    status = guest_->CreateSubVmar(magma_dev_mem_offset, magma_dev_mem_size, &magma_vmar);
    if (status != ZX_OK) {
      FX_PLOGS(INFO, status) << "Could not create VMAR for magma device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = pci_bus_->Connect(magma_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(INFO, status) << "Could not connect magma device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::VirtioWaylandImporter>
        wayland_importer_handle = nullptr;
    if (cfg.has_wayland_device()) {
      status = wl_->GetImporter(wayland_importer_handle.NewRequest());
      if (status != ZX_OK) {
        FX_PLOGS(INFO, status) << "Could not get wayland importer";
        return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
      }
    }
    status = magma_->Start(guest_->object(), std::move(magma_vmar),
                           std::move(wayland_importer_handle), context, dispatcher);
    if (status == ZX_ERR_NOT_FOUND) {
      FX_LOGS(INFO) << "Magma device not supported by host";
    } else if (status != ZX_OK) {
      FX_PLOGS(INFO, status) << "Could not start magma device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
  }

  // Setup sound device.
  if (cfg.has_virtio_sound() && cfg.virtio_sound()) {
    sound_ = std::make_unique<VirtioSound>(guest_->phys_mem());
    status = pci_bus_->Connect(sound_->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect sound device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = sound_->Start(guest_->object(), context, dispatcher,
                           cfg.has_virtio_sound_input() && cfg.virtio_sound_input());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to start sound device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
  }

  // Setup net device. We setup networking last, as this can cause a temporary loss of network
  // access as we configure the bridge. If networking is lost while loading packages for devices,
  // the VMM will fail.
  for (auto& net_device : *cfg.mutable_net_devices()) {
    auto net = std::make_unique<VirtioNet>(guest_->phys_mem());
    status = pci_bus_->Connect(net->pci_device(), dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to connect Ethernet device";
      return fit::error(GuestError::DEVICE_INITIALIZATION_FAILURE);
    }
    status = net->Start(guest_->object(), net_device.mac_address, net_device.enable_bridge, context,
                        dispatcher, net_devices_.size());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Could not open Ethernet device";
      return fit::error(GuestError::DEVICE_START_FAILURE);
    }
    net_devices_.push_back(std::move(net));
  }

#if __x86_64__
  if (auto result = CreatePageTable(guest_->phys_mem()); result.is_error()) {
    FX_PLOGS(ERROR, result.status_value()) << "Failed to create page table";
    return fit::error(GuestError::INTERNAL_ERROR);
  }

  const AcpiConfig acpi_cfg = {
      .dsdt_path = kDsdtPath,
      .mcfg_path = kMcfgPath,
      .io_apic_addr = IoApic::kPhysBase,
      .cpus = cfg.cpus(),
  };
  status = create_acpi_table(acpi_cfg, guest_->phys_mem());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create ACPI table";
    return fit::error(GuestError::INTERNAL_ERROR);
  }
#endif  // __x86_64__

  // Add any trap ranges as device memory.
  for (const IoMapping& mapping : guest_->mappings()) {
    if ((mapping.kind() == ZX_GUEST_TRAP_MEM || mapping.kind() == ZX_GUEST_TRAP_BELL) &&
        !dev_mem.AddRange(mapping.base(), mapping.size())) {
      FX_LOGS(ERROR) << "Failed to add trap range as device memory";
      return fit::error(GuestError::INTERNAL_ERROR);
    }
  }

  // Device memory has been finalized. Ensure that there's no overlap with the generated guest
  // memory ranges.
  dev_mem.Finalize();
  if (dev_mem.HasGuestMemoryOverlap(guest_->memory_regions())) {
    // Logs faulty guest ranges internally.
    return fit::error(GuestError::DEVICE_MEMORY_OVERLAP);
  }

  // Setup kernel.
  switch (cfg.kernel_type()) {
    case fuchsia::virtualization::KernelType::ZIRCON:
      status = setup_zircon(&cfg, guest_->phys_mem(), dev_mem, guest_->memory_regions(),
                            platform_devices_, &entry_, &boot_ptr_);
      break;
    case fuchsia::virtualization::KernelType::LINUX:
      status = setup_linux(&cfg, guest_->phys_mem(), dev_mem, guest_->memory_regions(),
                           platform_devices_, &entry_, &boot_ptr_);
      break;
  }
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to load kernel";
    return fit::error(GuestError::KERNEL_LOAD_FAILURE);
  }

  auto result = AddPublicServices();
  if (result.is_error()) {
    return result;
  }

  return fit::ok();
}

fit::result<GuestError> Vmm::StartPrimaryVcpu(
    fit::function<void(fit::result<GuestError>)> stop_callback) {
  guest_->set_stop_callback(std::move(stop_callback));
  zx_status_t status = guest_->StartVcpu(/*id=*/0, entry_, boot_ptr_);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to start VCPU-0";
    return fit::error(GuestError::VCPU_START_FAILURE);
  }

  return fit::ok();
}

zx_gpaddr_t Vmm::AllocDeviceAddr(size_t device_size) {
  const zx_gpaddr_t ret = next_device_address_;
  next_device_address_ += device_size;
  return ret;
}

fit::result<GuestError> Vmm::AddPublicServices() {
  zx_status_t status = outgoing_->AddPublicService(guest_bindings_.GetHandler(this));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to add guest controller public service";
    return fit::error(GuestError::DUPLICATE_PUBLIC_SERVICES);
  }
  return fit::ok();
}

void Vmm::GetSerial(GetSerialCallback callback) {
  FX_CHECK(client_serial_socket_.is_valid());
  callback(DuplicateSocket(client_serial_socket_));
}

void Vmm::GetConsole(GetConsoleCallback callback) {
  if (console_) {
    FX_CHECK(client_serial_socket_.is_valid());
    callback(fpromise::ok(DuplicateSocket(client_console_socket_)));
  } else {
    FX_LOGS(WARNING) << "Attempted to get console socket, but the console device is not present";
    callback(fpromise::error(GuestError::DEVICE_NOT_PRESENT));
  }
}

void Vmm::GetHostVsockEndpoint(fidl::InterfaceRequest<HostVsockEndpoint> endpoint,
                               GetHostVsockEndpointCallback callback) {
  if (vsock_) {
    vsock_->GetHostVsockEndpoint(std::move(endpoint));
    callback(fpromise::ok());
  } else {
    FX_LOGS(WARNING) << "Attempted to get HostVsockEndpoint, but the vsock device is not present";
    callback(fpromise::error(GuestError::DEVICE_NOT_PRESENT));
  }
}

void Vmm::GetBalloonController(fidl::InterfaceRequest<BalloonController> endpoint,
                               GetBalloonControllerCallback callback) {
  if (balloon_) {
    balloon_->ConnectToBalloonController(std::move(endpoint));
    callback(fpromise::ok());
  } else {
    FX_LOGS(WARNING) << "Attempted to get BalloonController, but the balloon device is not present";
    callback(fpromise::error(GuestError::DEVICE_NOT_PRESENT));
  }
}

void Vmm::NotifyClientsShutdown(zx_status_t status) { guest_bindings_.CloseAll(status); }

}  // namespace vmm
