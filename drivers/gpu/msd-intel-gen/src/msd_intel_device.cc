// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "register_defs.h"

MsdIntelDevice::MsdIntelDevice() { magic_ = kMagic; }

std::unique_ptr<MsdIntelConnection> MsdIntelDevice::Open(msd_client_id client_id)
{
    return std::unique_ptr<MsdIntelConnection>(new MsdIntelConnection());
}

bool MsdIntelDevice::Init(void* device_handle)
{
    DASSERT(!platform_device_);

    DLOG("Init device_handle %p", device_handle);

    platform_device_ = magma::PlatformDevice::Create(device_handle);
    if (!platform_device_)
        return DRETF(false, "failed to create platform device");

    uint16_t pci_dev_id;
    if (!platform_device_->ReadPciConfig16(2, &pci_dev_id))
        return DRETF(false, "ReadPciConfig16 failed");

    device_id_ = pci_dev_id;
    DLOG("device_id 0x%x", device_id_);

    unsigned int gtt_size;
    if (!ReadGttSize(&gtt_size))
        return DRETF(false, "failed to read gtt size");

    DLOG("gtt_size: %uMB", gtt_size >> 20);

    std::unique_ptr<magma::PlatformMmio> mmio(
        platform_device_->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE));
    if (!mmio)
        return DRETF(false, "failed to map pci bar 0");

    register_io_ = std::unique_ptr<RegisterIo>(new RegisterIo(std::move(mmio)));

    gtt_ = std::unique_ptr<Gtt>(new Gtt(this));

    if (!gtt_->Init(gtt_size, platform_device_.get()))
        return DRETF(false, "failed to Init gtt");

    render_engine_cs_ =
        std::unique_ptr<RenderEngineCommandStreamer>(new RenderEngineCommandStreamer());

    default_context_ = std::unique_ptr<MsdIntelContext>(new MsdIntelContext());

    if (!render_engine_cs_->InitContext(default_context_.get()))
        return DRETF(false, "failed to init render engine command streamer");

    if (!default_context_->MapGpu(gtt_.get(), render_engine_cs_->id()))
        return DRETF(false, "failed to pin default context");

    return true;
}

bool MsdIntelDevice::ReadGttSize(unsigned int* gtt_size)
{
    DASSERT(platform_device_);

    uint16_t reg;
    if (!platform_device_->ReadPciConfig16(GMCH_GFX_CTRL, &reg))
        return DRETF(false, "ReadPciConfig16 failed");

    unsigned int size = (reg >> 6) & 0x3;
    *gtt_size = (size == 0) ? 0 : (1 << size) * 1024 * 1024;

    return true;
}

//////////////////////////////////////////////////////////////////////////////

msd_connection* msd_device_open(msd_device* dev, msd_client_id client_id)
{
    // here we open the connection and transfer ownership of the result across the ABI
    return MsdIntelDevice::cast(dev)->Open(client_id).release();
}

uint32_t msd_device_get_id(msd_device* dev) { return MsdIntelDevice::cast(dev)->device_id(); }
