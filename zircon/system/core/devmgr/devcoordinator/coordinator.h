// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>

#include <utility>

#include "boot-args.h"
#include "composite-device.h"
#include "devhost.h"
#include "device.h"
#include "driver.h"
#include "metadata.h"
#include "vmo-writer.h"

namespace devmgr {

class DevhostLoaderService;

class SuspendContext {
public:
    enum class Flags : uint32_t {
        kRunning = 0u,
        kSuspend = 1u,
    };

    SuspendContext() = default;

    SuspendContext(Coordinator* coordinator, Flags flags, uint32_t sflags, zx::socket socket,
                   zx::vmo kernel = zx::vmo(), zx::vmo bootdata = zx::vmo())
        : coordinator_(coordinator), flags_(flags), sflags_(sflags), socket_(std::move(socket)),
          kernel_(std::move(kernel)), bootdata_(std::move(bootdata)) {}

    ~SuspendContext() { devhosts_.clear(); }

    SuspendContext(SuspendContext&&) = default;
    SuspendContext& operator=(SuspendContext&&) = default;

    void ContinueSuspend(const zx::resource& root_resource);

    Coordinator* coordinator() { return coordinator_; }

    zx_status_t status() const { return status_; }
    void set_status(zx_status_t status) { status_ = status; }
    Flags flags() const { return flags_; }
    void set_flags(Flags flags) { flags_ = flags; }
    uint32_t sflags() const { return sflags_; }

    Devhost* dh() const { return dh_; }
    void set_dh(Devhost* dh) { dh_ = dh; }

    using DevhostList = fbl::DoublyLinkedList<Devhost*, Devhost::SuspendNode>;
    DevhostList& devhosts() { return devhosts_; }
    const DevhostList& devhosts() const { return devhosts_; }

    const zx::vmo& kernel() const { return kernel_; }
    const zx::vmo& bootdata() const { return bootdata_; }

    // Close the socket whose ownership was handed to this SuspendContext.
    void CloseSocket() { socket_.reset(); }

    // The AddRef and Release functions follow the contract for fbl::RefPtr.
    void AddRef() const { ++count_; }

    // Returns true when the last message reference has been released.
    bool Release() const {
        const int32_t rc = count_;
        --count_;
        return rc == 1;
    }

private:
    Coordinator* coordinator_ = nullptr;

    zx_status_t status_ = ZX_OK;
    Flags flags_ = Flags::kRunning;

    // suspend flags
    uint32_t sflags_ = 0u;
    // outstanding msgs
    mutable uint32_t count_ = 0u;
    // next devhost to process
    Devhost* dh_ = nullptr;
    fbl::DoublyLinkedList<Devhost*, Devhost::SuspendNode> devhosts_;

    // socket to notify on for 'dm reboot' and 'dm poweroff'
    zx::socket socket_;

    // mexec arguments
    zx::vmo kernel_;
    zx::vmo bootdata_;
};

// Values parsed out of argv.  All paths described below are absolute paths.
struct DevmgrArgs {
    // Load drivers from these directories.  If this is empty, the default will
    // be used.
    fbl::Vector<const char*> driver_search_paths;
    // Load the drivers with these paths.  The specified drivers do not need to
    // be in directories in |driver_search_paths|.
    fbl::Vector<const char*> load_drivers;
    // Use this driver as the sys_device driver.  If nullptr, the default will
    // be used.
    const char* sys_device_driver = nullptr;
    // Select whether to launch a new svchost or to just use the system provided
    // /svc directory.
    bool use_system_svchost = false;
    // Disables the block watcher if set to true. This can be used for testing purposes,
    // where it is not necessary to have the block watcher running.
    bool disable_block_watcher = false;
    // Disables the netsvc if set to true. This can be used for testing purposes,
    // where it is not necessary to have the netsvc running.
    bool disable_netsvc = false;
};

struct CoordinatorConfig {
    // Initial root resource from the kernel.
    zx::resource root_resource;
    // Job for sysinfo.
    zx::job sysinfo_job;
    // Job for all devhosts.
    zx::job devhost_job;
    // Event that controls the fshost.
    zx::event fshost_event;
    // Async dispatcher for the coordinator.
    async_dispatcher_t* dispatcher;
    // Boot arguments from the Arguments service.
    const devmgr::BootArgs* boot_args;
    // If true, netsvc is disabled and will not start.
    bool disable_netsvc;
    // Whether we require /system.
    bool require_system;
    // Whether we require ASan drivers.
    bool asan_drivers;
    // Whether to reboot the device when suspend does not finish on time.
    bool suspend_fallback;
    // Whether to print out debugging when suspend does not finish on time.
    bool suspend_debug;
};

class Coordinator {
public:
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;
    Coordinator(Coordinator&&) = delete;
    Coordinator& operator=(Coordinator&&) = delete;

    explicit Coordinator(CoordinatorConfig config);
    ~Coordinator();

    zx_status_t InitializeCoreDevices(const char* sys_device_driver);
    bool InSuspend() const;

    zx_status_t ScanSystemDrivers();
    void BindDrivers();
    void UseFallbackDrivers();
    void DriverAdded(Driver* drv, const char* version);
    void DriverAddedInit(Driver* drv, const char* version);
    zx_status_t LibnameToVmo(const fbl::String& libname, zx::vmo* out_vmo) const;

    // Attempts to bind the given driver to the given device.  Returns ZX_OK on
    // success, ZX_ERR_NEXT if the driver is not capable of binding to the device,
    // and a different error if the driver was capable of binding but failed to bind.
    zx_status_t BindDriverToDevice(const fbl::RefPtr<Device>& dev, const Driver* drv,
                                   bool autobind);

    // Used to implement fuchsia::device::manager::Coordinator.
    zx_status_t AddDevice(const fbl::RefPtr<Device>& parent, zx::channel rpc,
                          const uint64_t* props_data, size_t props_count, fbl::StringPiece name,
                          uint32_t protocol_id, fbl::StringPiece driver_path, fbl::StringPiece args,
                          bool invisible, zx::channel client_remote,
                          fbl::RefPtr<Device>* new_device);
    zx_status_t RemoveDevice(const fbl::RefPtr<Device>& dev, bool forced);
    zx_status_t MakeVisible(const fbl::RefPtr<Device>& dev);
    zx_status_t BindDevice(const fbl::RefPtr<Device>& dev, fbl::StringPiece drvlibname,
                           bool new_device);
    zx_status_t GetTopologicalPath(const fbl::RefPtr<const Device>& dev, char* out,
                                   size_t max) const;
    zx_status_t LoadFirmware(const fbl::RefPtr<Device>& dev, const char* path, zx::vmo* vmo,
                             size_t* size);

    zx_status_t GetMetadata(const fbl::RefPtr<Device>& dev, uint32_t type, void* buffer,
                            size_t buflen, size_t* size);
    zx_status_t GetMetadataSize(const fbl::RefPtr<Device>& dev, uint32_t type, size_t* size) {
        return GetMetadata(dev, type, nullptr, 0, size);
    }
    zx_status_t AddMetadata(const fbl::RefPtr<Device>& dev, uint32_t type, const void* data,
                            uint32_t length);
    zx_status_t PublishMetadata(const fbl::RefPtr<Device>& dev, const char* path, uint32_t type,
                                const void* data, uint32_t length);
    zx_status_t AddCompositeDevice(const fbl::RefPtr<Device>& dev, fbl::StringPiece name,
                                   const zx_device_prop_t* props_data, size_t props_count,
                                   const fuchsia_device_manager_DeviceComponent* components,
                                   size_t components_count, uint32_t coresident_device_index);

    zx_status_t DmCommand(size_t len, const char* cmd);
    zx_status_t DmOpenVirtcon(zx::channel virtcon_receiver) const;
    void DmMexec(zx::vmo kernel, zx::vmo bootdata);

    void HandleNewDevice(const fbl::RefPtr<Device>& dev);
    zx_status_t PrepareProxy(const fbl::RefPtr<Device>& dev, Devhost* target_devhost);

    void DumpState(VmoWriter* vmo) const;
    void DumpDrivers(VmoWriter* vmo) const;
    void DumpGlobalDeviceProps(VmoWriter* vmo) const;

    const zx::resource& root_resource() const { return config_.root_resource; }
    const zx::event& fshost_event() const { return config_.fshost_event; }
    async_dispatcher_t* dispatcher() const { return config_.dispatcher; }
    const devmgr::BootArgs& boot_args() const { return *config_.boot_args; }
    bool disable_netsvc() const { return config_.disable_netsvc; }
    bool require_system() const { return config_.require_system; }
    bool suspend_fallback() const { return config_.suspend_fallback; }
    bool suspend_debug() const { return config_.suspend_debug; }

    void set_running(bool running) { running_ = running; }
    bool system_available() const { return system_available_; }
    void set_system_available(bool system_available) { system_available_ = system_available; }
    bool system_loaded() const { return system_loaded_; }
    void set_loader_service(DevhostLoaderService* loader_service) {
        loader_service_ = loader_service;
    }
    void set_virtcon_channel(zx::channel virtcon_channel) {
        virtcon_channel_ = std::move(virtcon_channel);
    }
    void set_dmctl_socket(zx::socket dmctl_socket) { dmctl_socket_ = std::move(dmctl_socket); }

    fbl::DoublyLinkedList<Driver*, Driver::Node>& drivers() { return drivers_; }
    const fbl::DoublyLinkedList<Driver*, Driver::Node>& drivers() const { return drivers_; }
    fbl::DoublyLinkedList<fbl::RefPtr<Device>, Device::AllDevicesNode>& devices() {
        return devices_;
    }
    const fbl::DoublyLinkedList<fbl::RefPtr<Device>, Device::AllDevicesNode>& devices() const {
        return devices_;
    }

    void AppendPublishedMetadata(fbl::unique_ptr<Metadata> metadata) {
        published_metadata_.push_back(std::move(metadata));
    }

    const fbl::RefPtr<Device>& root_device() { return root_device_; }
    const fbl::RefPtr<Device>& misc_device() { return misc_device_; }
    const fbl::RefPtr<Device>& sys_device() { return sys_device_; }
    const fbl::RefPtr<Device>& test_device() { return test_device_; }

    void Suspend(uint32_t flags);

    SuspendContext& suspend_context() { return suspend_context_; }
    const SuspendContext& suspend_context() const { return suspend_context_; }

    zx_status_t BindFidlServiceProxy(zx::channel listen_on);

    const Driver* component_driver() const { return component_driver_; }

    void ReleaseDevhost(Devhost* dh);
private:
    CoordinatorConfig config_;
    bool running_ = false;
    bool launched_first_devhost_ = false;
    bool system_available_ = false;
    bool system_loaded_ = false;
    DevhostLoaderService* loader_service_ = nullptr;

    // Channel for creating new virtual consoles.
    zx::channel virtcon_channel_;
    // This socket is used by DmPrintf for output, and DmPrintf can be called in
    // the context of a const member function, therefore it is also const. Given
    // that, we must make dmctl_socket_ mutable.
    mutable zx::socket dmctl_socket_;

    // All Drivers
    fbl::DoublyLinkedList<Driver*, Driver::Node> drivers_;

    // Drivers to try last
    fbl::DoublyLinkedList<Driver*, Driver::Node> fallback_drivers_;

    // List of drivers loaded from /system by system_driver_loader()
    fbl::DoublyLinkedList<Driver*, Driver::Node> system_drivers_;

    // All Devices (excluding static immortal devices)
    fbl::DoublyLinkedList<fbl::RefPtr<Device>, Device::AllDevicesNode> devices_;

    // All DevHosts
    fbl::DoublyLinkedList<Devhost*, Devhost::AllDevhostsNode> devhosts_;

    // All composite devices
    fbl::DoublyLinkedList<std::unique_ptr<CompositeDevice>, CompositeDevice::Node>
            composite_devices_;

    fbl::RefPtr<Device> root_device_;
    fbl::RefPtr<Device> misc_device_;
    fbl::RefPtr<Device> sys_device_;
    fbl::RefPtr<Device> test_device_;

    SuspendContext suspend_context_;

    fbl::DoublyLinkedList<fbl::unique_ptr<Metadata>, Metadata::Node> published_metadata_;

    // Once the special component driver is loaded, this will refer to it.  This
    // driver is used for binding against components of composite devices
    const Driver* component_driver_ = nullptr;

    void DumpDevice(VmoWriter* vmo, const Device* dev, size_t indent) const;
    void DumpDeviceProps(VmoWriter* vmo, const Device* dev) const;

    void BuildSuspendList();
    void Suspend(SuspendContext ctx);

    fbl::unique_ptr<Driver> ValidateDriver(fbl::unique_ptr<Driver> drv);
    const Driver* LibnameToDriver(const fbl::String& libname) const;

    zx_status_t NewDevhost(const char* name, Devhost* parent, Devhost** out);

    zx_status_t BindDriver(Driver* drv);
    zx_status_t AttemptBind(const Driver* drv, const fbl::RefPtr<Device>& dev);
    void BindSystemDrivers();
    void DriverAddedSys(Driver* drv, const char* version);

    zx_status_t GetMetadataRecurse(const fbl::RefPtr<Device>& dev, uint32_t type, void* buffer,
                                   size_t buflen, size_t* size);
};

bool driver_is_bindable(const Driver* drv, uint32_t protocol_id,
                        const fbl::Array<const zx_device_prop_t>& props, bool autobind);

// Path to driver that should be bound to components of composite devices
extern const char* kComponentDriverPath;

zx_status_t fidl_DmCommand(void* ctx, zx_handle_t raw_log_socket, const char* command_data,
                           size_t command_size, fidl_txn_t* txn);
zx_status_t fidl_DmOpenVirtcon(void* ctx, zx_handle_t raw_vc_receiver);
zx_status_t fidl_DmMexec(void* ctx, zx_handle_t raw_kernel, zx_handle_t raw_bootdata);
zx_status_t fidl_DirectoryWatch(void* ctx, uint32_t mask, uint32_t options,
                                zx_handle_t raw_watcher, fidl_txn_t* txn);

} // namespace devmgr
