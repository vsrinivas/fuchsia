// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device.h"
#include "forcewake.h"
#include "global_context.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "register_defs.h"
#include "registers.h"
#include <cstdio>
#include <string>

MsdIntelDevice::MsdIntelDevice() { magic_ = kMagic; }

std::unique_ptr<MsdIntelConnection> MsdIntelDevice::Open(msd_client_id client_id)
{
    return std::unique_ptr<MsdIntelConnection>(new MsdIntelConnection(this));
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

    if (is_gen8(device_id_)) {
        ForceWake::reset(register_io_.get(), registers::ForceWake::GEN8);
        ForceWake::request(register_io_.get(), registers::ForceWake::GEN8);
    } else if (is_gen9(device_id_)) {
        ForceWake::reset(register_io_.get(), registers::ForceWake::GEN9_RENDER);
        ForceWake::request(register_io_.get(), registers::ForceWake::GEN9_RENDER);
    } else {
        DASSERT(false);
    }

    // Clear faults
    registers::AllEngineFault::clear(register_io_.get());

    gtt_ = std::unique_ptr<Gtt>(new Gtt(this));

    if (!gtt_->Init(gtt_size, platform_device_.get()))
        return DRETF(false, "failed to Init gtt");

    // Arbitrary
    constexpr uint32_t kFirstSequenceNumber = 0x1000;
    sequencer_ = std::unique_ptr<Sequencer>(new Sequencer(kFirstSequenceNumber));

    render_engine_cs_ = RenderEngineCommandStreamer::Create(this, gtt_.get());

    auto context = std::unique_ptr<GlobalContext>(new GlobalContext());

    // Creates the context backing store.
    if (!render_engine_cs_->InitContext(context.get()))
        return DRETF(false, "render_engine_cs failed to init global context");

    if (!context->Map(gtt_.get(), render_engine_cs_->id()))
        return DRETF(false, "global context init failed");

    render_engine_cs_->InitHardware(context->hardware_status_page(render_engine_cs_->id()));

    if (!render_engine_cs_->RenderInit(context.get()))
        return DRETF(false, "render_engine_cs failed RenderInit");

    global_context_ = std::move(context);

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

void MsdIntelDevice::Dump(DumpState* dump_out)
{
    dump_out->render_cs.sequence_number =
        global_context_->hardware_status_page(render_engine_cs_->id())->read_sequence_number();
    dump_out->render_cs.active_head_pointer = render_engine_cs_->GetActiveHeadPointer();

    DumpFault(dump_out, registers::AllEngineFault::read(register_io_.get()));
}

void MsdIntelDevice::DumpFault(DumpState* dump_out, uint32_t fault)
{
    dump_out->fault_present = registers::AllEngineFault::valid(fault);
    dump_out->fault_engine = registers::AllEngineFault::engine(fault);
    dump_out->fault_src = registers::AllEngineFault::src(fault);
    dump_out->fault_type = registers::AllEngineFault::type(fault);
}

void MsdIntelDevice::DumpToString(std::string& dump_out)
{
    DumpState dump_state;
    Dump(&dump_state);

    const char* fmt = "Device id: 0x%x\n"
                      "RENDER_COMMAND_STREAMER\n"
                      "sequence_number 0x%x\n"
                      "active head pointer: 0x%llx\n";
    int size = std::snprintf(nullptr, 0, fmt, device_id(), dump_state.render_cs.sequence_number,
                             dump_state.render_cs.active_head_pointer);
    std::vector<char> buf(size + 1);
    std::snprintf(&buf[0], buf.size(), fmt, device_id(), dump_state.render_cs.sequence_number,
                  dump_state.render_cs.active_head_pointer);
    dump_out.append(&buf[0]);

    if (dump_state.fault_present) {
        fmt = "ENGINE FAULT DETECTED\n"
              "engine 0x%x src 0x%x type 0x%x\n";
        size = std::snprintf(nullptr, 0, fmt, dump_state.fault_engine, dump_state.fault_src,
                             dump_state.fault_type);
        std::vector<char> buf(size + 1);
        std::snprintf(&buf[0], buf.size(), fmt, dump_state.fault_engine, dump_state.fault_src,
                      dump_state.fault_type);
        dump_out.append(&buf[0]);
    } else {
        dump_out.append("No engine faults detected.");
    }
}

//////////////////////////////////////////////////////////////////////////////

msd_connection* msd_device_open(msd_device* dev, msd_client_id client_id)
{
    // here we open the connection and transfer ownership of the result across the ABI
    return MsdIntelDevice::cast(dev)->Open(client_id).release();
}

uint32_t msd_device_get_id(msd_device* dev) { return MsdIntelDevice::cast(dev)->device_id(); }

void msd_device_dump_status(struct msd_device* dev)
{
    std::string dump;
    MsdIntelDevice::cast(dev)->DumpToString(dump);
    DLOG("--------------------\n%s\n--------------------\n", dump.c_str());
}
