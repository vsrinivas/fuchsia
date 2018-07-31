// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <threads.h>
#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/canvas.h>
#include <ddktl/protocol/clk.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/i2c-impl.h>
#include <ddktl/protocol/iommu.h>
#include <ddktl/protocol/mailbox.h>
#include <ddktl/protocol/platform-bus.h>
#include <ddktl/protocol/scpi.h>
#include <ddktl/protocol/usb-mode-switch.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/sync/completion.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

#include "platform-device.h"
#include "platform-i2c.h"

typedef struct pdev_req pdev_req_t;

namespace platform_bus {

class PlatformBus;
using PlatformBusType = ddk::Device<PlatformBus, ddk::GetProtocolable>;

// This is the main class for the platform bus driver.
class PlatformBus : public PlatformBusType, public ddk::PlatformBusProtocol<PlatformBus>,
                    public ddk::IommuProtocol<PlatformBus> {
public:
    static zx_status_t Create(zx_device_t* parent, const char* name, zx::vmo zbi);

    // Device protocol implementation.
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    void DdkRelease();

    // Platform bus protocol implementation.
    zx_status_t SetProtocol(uint32_t proto_id, void* protocol);
    zx_status_t WaitProtocol(uint32_t proto_id);
    zx_status_t DeviceAdd(const pbus_dev_t* dev, uint32_t flags);
    zx_status_t DeviceEnable(uint32_t vid, uint32_t pid, uint32_t did, bool enable);
    const char* GetBoardName();

    // IOMMU protocol implementation.
    zx_status_t GetBti(uint32_t iommu_index, uint32_t bti_id, zx_handle_t* out_handle);

    // Returns the resource handle to be used for creating MMIO regions and IRQs.
    // Currently this just returns the root resource, but we may change this to a more
    // limited resource in the future.
    zx_handle_t GetResource() const { return get_root_resource(); }

    // Used by PlatformDevice to queue I2C transactions on an I2C bus.
    zx_status_t I2cTransact(pdev_req_t* req, pbus_i2c_channel_t* channel,
                            const void* write_buf, zx_handle_t channel_handle);

    // Protocol accessors for PlatformDevice.
    inline ddk::CanvasProtocolProxy* canvas() const { return canvas_.get(); }
    inline ddk::ClkProtocolProxy* clk() const { return clk_.get(); }
    inline ddk::GpioProtocolProxy* gpio() const { return gpio_.get(); }
    inline ddk::I2cImplProtocolProxy* i2c_impl() const { return i2c_impl_.get(); }
    inline ddk::ScpiProtocolProxy* scpi() const { return scpi_.get(); }
    inline ddk::UmsProtocolProxy* ums() const { return ums_.get(); }
    inline const uint8_t* metadata() const { return metadata_.get(); }
    inline size_t metadata_size() const { return metadata_.size(); }

private:
    explicit PlatformBus(zx_device_t* parent);

    DISALLOW_COPY_ASSIGN_AND_MOVE(PlatformBus);

    zx_status_t Init(zx::vmo zbi);

    // Reads the platform ID and driver metadata records from the boot image.
    zx_status_t ReadZbi(zx::vmo zbi);

    zx_status_t I2cInit(i2c_impl_protocol_t* i2c);

    zbi_platform_id_t platform_id_;

    // Protocols that are optionally provided by the board driver.
    fbl::unique_ptr<ddk::CanvasProtocolProxy> canvas_;
    fbl::unique_ptr<ddk::ClkProtocolProxy> clk_;
    fbl::unique_ptr<ddk::GpioProtocolProxy> gpio_;
    fbl::unique_ptr<ddk::IommuProtocolProxy> iommu_;
    fbl::unique_ptr<ddk::I2cImplProtocolProxy> i2c_impl_;
    fbl::unique_ptr<ddk::ScpiProtocolProxy> scpi_;
    fbl::unique_ptr<ddk::MailboxProtocolProxy> mailbox_;
    fbl::unique_ptr<ddk::UmsProtocolProxy> ums_;

    // Completion used by WaitProtocol().
    sync_completion_t proto_completion_ __TA_GUARDED(mutex_);

    fbl::Mutex mutex_;

    // Metadata extracted from ZBI.
    fbl::Array<uint8_t> metadata_;

    // List of platform devices.
    fbl::Vector<fbl::unique_ptr<PlatformDevice>> devices_;
    // List of I2C buses.
    fbl::Vector<fbl::unique_ptr<PlatformI2cBus>> i2c_buses_;

    // Dummy IOMMU.
    zx::handle iommu_handle_;
};

} // namespace platform_bus

__BEGIN_CDECLS
zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name,
                                const char* args, zx_handle_t rpc_channel);
__END_CDECLS
