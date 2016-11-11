// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_DEVICE_H
#define MSD_DEVICE_H

#include "engine_command_streamer.h"
#include "global_context.h"
#include "gtt.h"
#include "magma_util/macros.h"
#include "msd.h"
#include "msd_intel_connection.h"
#include "msd_intel_context.h"
#include "platform_device.h"
#include "register_io.h"
#include "sequencer.h"
#include <deque>

class MsdIntelDevice : public msd_device,
                       public Gtt::Owner,
                       public EngineCommandStreamer::Owner,
                       public MsdIntelConnection::Owner {
public:
    virtual ~MsdIntelDevice() {}

    // This takes ownership of the connection so that ownership can be
    // transferred across the MSD ABI by the caller
    std::unique_ptr<MsdIntelConnection> Open(msd_client_id client_id);

    uint32_t device_id() { return device_id_; }

    static MsdIntelDevice* cast(msd_device* dev)
    {
        DASSERT(dev);
        DASSERT(dev->magic_ == kMagic);
        return static_cast<MsdIntelDevice*>(dev);
    }

    bool Init(void* device_handle);

    struct DumpState {
        struct RenderCommandStreamer {
            uint32_t sequence_number;
            uint64_t active_head_pointer;
        } render_cs;

        bool fault_present;
        uint8_t fault_engine;
        uint8_t fault_src;
        uint8_t fault_type;
        uint64_t fault_gpu_address;
    };

    void Flip(std::shared_ptr<MsdIntelBuffer> buffer, magma_system_pageflip_callback_t callback,
              void* data);

    void Dump(DumpState* dump_state);
    void DumpToString(std::string& dump_string);

private:
    MsdIntelDevice();

    // Gtt::Owner, EngineCommandStreamer::Owner
    RegisterIo* register_io() override
    {
        DASSERT(register_io_);
        return register_io_.get();
    }

    Sequencer* sequencer() override
    {
        DASSERT(sequencer_);
        return sequencer_.get();
    }

    HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id) override;

    // MsdIntelConnection::Owner
    bool ExecuteCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf) override;
    bool WaitRendering(std::shared_ptr<MsdIntelBuffer> buf) override;

    bool WaitIdle();
    bool ReadGttSize(unsigned int* gtt_size);

    uint32_t GetCurrentFrequency();
    void RequestMaxFreq();

    static void DumpFault(DumpState* dump_out, uint32_t fault);
    static void DumpFaultAddress(DumpState* dump_out, RegisterIo* register_io);

    std::shared_ptr<GlobalContext> global_context() { return global_context_; }

    RenderEngineCommandStreamer* render_engine_cs() { return render_engine_cs_.get(); }

    std::shared_ptr<AddressSpace> gtt() { return gtt_; }

    static const uint32_t kMagic = 0x64657669; //"devi"

    uint32_t device_id_{};

    std::unique_ptr<magma::PlatformDevice> platform_device_;
    std::unique_ptr<RegisterIo> register_io_;
    std::shared_ptr<Gtt> gtt_;
    std::unique_ptr<RenderEngineCommandStreamer> render_engine_cs_;
    std::shared_ptr<GlobalContext> global_context_;
    std::unique_ptr<Sequencer> sequencer_;

    // page flipping
    std::deque<std::shared_ptr<GpuMapping>> display_mappings_;
    magma_system_pageflip_callback_t flip_callback_{};
    void* flip_data_{};

    friend class MsdIntelDriver;
    friend class TestMsdIntelDevice;
    friend class TestCommandBuffer;
};

#endif // MSD_DEVICE_H
