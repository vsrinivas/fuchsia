// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>
#include <magenta/assert.h>
#include <magenta/cpp.h>
#include <mxtl/auto_lock.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "drivers/audio/dispatcher-pool/dispatcher-thread.h"
#include "drivers/audio/intel-hda/utils/intel-hda-registers.h"
#include "drivers/audio/intel-hda/utils/intel-hda-proto.h"

#include "debug-logging.h"
#include "intel-hda-codec.h"
#include "intel-hda-controller.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

// static member variable declaration
constexpr uint        IntelHDAController::RIRB_RESERVED_RESPONSE_SLOTS;
mx_driver_t*          IntelHDAController::driver_;
mxtl::atomic_uint32_t IntelHDAController::device_id_gen_(0u);

// Device interface thunks
#define DEV(_dev)  static_cast<IntelHDAController*>((_dev)->ctx)
mx_protocol_device_t IntelHDAController::CONTROLLER_DEVICE_THUNKS = {
    .get_protocol = nullptr,
    .open         = nullptr,
    .openat       = nullptr,
    .close        = nullptr,
    .unbind       = [](mx_device_t* dev) { DEV(dev)->DeviceShutdown(); },
    .release      = [](mx_device_t* dev) -> mx_status_t { return DEV(dev)->DeviceRelease(); },
    .read         = nullptr,
    .write        = nullptr,
    .iotxn_queue  = nullptr,
    .get_size     = nullptr,
    .ioctl        = [](mx_device_t* dev,
                       uint32_t     op,
                       const void*  in_buf,
                       size_t       in_len,
                       void*        out_buf,
                       size_t       out_len) -> ssize_t {
                        return DEV(dev)->DeviceIoctl(op, in_buf, in_len, out_buf, out_len);
                   },
    .suspend      = nullptr,
    .resume       = nullptr,
};
#undef DEV

void IntelHDAController::PrintDebugPrefix() const {
    printf("[%s] ", debug_tag_);
}

IntelHDAController::IntelHDAController()
    : state_(static_cast<StateStorage>(State::STARTING)),
      id_(device_id_gen_.fetch_add(1u)) {
    memset(&dev_node_, 0, sizeof(dev_node_));
    snprintf(debug_tag_, sizeof(debug_tag_), "Unknown IHDA Controller");
}

IntelHDAController::~IntelHDAController() {
    MX_DEBUG_ASSERT((GetState() == State::STARTING) || (GetState() == State::SHUT_DOWN));
    // TODO(johngro) : place the device into reset.

    // Release our register window.
    if (regs_handle_ != MX_HANDLE_INVALID) {
        MX_DEBUG_ASSERT(pci_proto_ != nullptr);
        mx_handle_close(regs_handle_);
    }

    // Release our IRQ event.
    if (irq_handle_ != MX_HANDLE_INVALID)
        mx_handle_close(irq_handle_);

    // Disable IRQs at the PCI level.
    if (pci_proto_ != nullptr) {
        MX_DEBUG_ASSERT(pci_dev_ != nullptr);
        pci_proto_->set_irq_mode(pci_dev_, MX_PCIE_IRQ_MODE_DISABLED, 0);
    }

    // Let go of our stream state.
    free_input_streams_.clear();
    free_output_streams_.clear();
    free_bidir_streams_.clear();

    // Release all of our physical memory used to talk directly to the hardware.
    cmd_buf_mem_.Release();
    bdl_mem_.Release();

    if (pci_dev_ != nullptr) {
        // TODO(johngro) : unclaim the PCI device.  Right now, there is no way
        // to do this aside from closing the device handle (which would
        // seriously mess up the DevMgr's brain)
        pci_dev_ = nullptr;
        pci_proto_ = nullptr;
    }
}

mxtl::RefPtr<IntelHDAStream> IntelHDAController::AllocateStream(IntelHDAStream::Type type) {
    mxtl::AutoLock lock(&stream_pool_lock_);
    IntelHDAStream::Tree* src;

    switch (type) {
    case IntelHDAStream::Type::INPUT:  src = &free_input_streams_;  break;
    case IntelHDAStream::Type::OUTPUT: src = &free_output_streams_; break;

    // Users are not allowed to directly request bidirectional stream contexts.
    // It's just what they end up with if there are no other choices.
    default:
        MX_DEBUG_ASSERT(false);
        return nullptr;
    }

    if (src->is_empty()) {
        src = &free_bidir_streams_;
        if (src->is_empty())
            return nullptr;
    }

    // Allocation fails if we cannot assign a unique tag to this stream.
    uint8_t stream_tag = AllocateStreamTagLocked(type == IntelHDAStream::Type::INPUT);
    if (!stream_tag)
        return nullptr;

    auto ret = src->pop_front();
    ret->Configure(type, stream_tag);

    return ret;
}

void IntelHDAController::ReturnStream(mxtl::RefPtr<IntelHDAStream>&& ptr) {
    mxtl::AutoLock lock(&stream_pool_lock_);
    ReturnStreamLocked(mxtl::move(ptr));
}

void IntelHDAController::ReturnStreamLocked(mxtl::RefPtr<IntelHDAStream>&& ptr) {
    IntelHDAStream::Tree* dst;

    MX_DEBUG_ASSERT(ptr);

    switch (ptr->type()) {
    case IntelHDAStream::Type::INPUT:  dst = &free_input_streams_;  break;
    case IntelHDAStream::Type::OUTPUT: dst = &free_output_streams_; break;
    case IntelHDAStream::Type::BIDIR:  dst = &free_bidir_streams_;  break;
    default: MX_DEBUG_ASSERT(false); return;
    }

    ptr->Configure(IntelHDAStream::Type::INVALID, 0);
    dst->insert(mxtl::move(ptr));
}

uint8_t IntelHDAController::AllocateStreamTagLocked(bool input) {
    uint16_t& tag_pool = input ? free_input_tags_ : free_output_tags_;

    for (uint8_t ret = 1; ret < (sizeof(tag_pool) << 3); ++ret) {
        if (tag_pool & (1u << ret)) {
            tag_pool = static_cast<uint16_t>(tag_pool & ~(1u << ret));
            return ret;
        }
    }

    return 0;
}

void IntelHDAController::ReleaseStreamTagLocked(bool input, uint8_t tag) {
    uint16_t& tag_pool = input ? free_input_tags_ : free_output_tags_;

    MX_DEBUG_ASSERT((tag > 0) && (tag <= 15));
    MX_DEBUG_ASSERT((tag_pool & (1u << tag)) == 0);

    tag_pool = static_cast<uint16_t>((tag_pool | (1u << tag)));
}

void IntelHDAController::ShutdownIRQThread() {
    if (irq_thread_started_) {
        SetState(State::SHUTTING_DOWN);
        WakeupIRQThread();
        thrd_join(irq_thread_, NULL);
        MX_DEBUG_ASSERT(GetState() == State::SHUT_DOWN);
        irq_thread_started_ = false;
    }
}

void IntelHDAController::DeviceShutdown() {
    // Make sure we have closed all of the channels clients are using to talk to
    // us, and that we have synchronized with any callbacks in flight.
    IntelHDADevice::Shutdown();

    // If the IRQ thread is running, make sure we shut it down too.
    ShutdownIRQThread();
}

mx_status_t IntelHDAController::DeviceRelease() {
    // Take our unmanaged reference back from our published device node.
    MX_DEBUG_ASSERT(dev_node_.ctx == this);
    auto thiz = mxtl::internal::MakeRefPtrNoAdopt(this);
    dev_node_.ctx = nullptr;

    // ASSERT that we have been properly shut down, then release the DDK's
    // reference to our state as we allow thiz to go out of scope.
    MX_DEBUG_ASSERT(GetState() == State::SHUT_DOWN);
    thiz.reset();

    return NO_ERROR;
}

mx_status_t IntelHDAController::ProcessClientRequest(DispatcherChannel& channel,
                                                     const RequestBufferType& req,
                                                     uint32_t req_size,
                                                     mx::handle&& rxed_handle) {
    if (req_size < sizeof(req.hdr)) {
        DEBUG_LOG("Client request too small to contain header (%u < %zu)\n",
                req_size, sizeof(req.hdr));
        return ERR_INVALID_ARGS;
    }

    VERBOSE_LOG("Client Request 0x%04x len %u\n", req.hdr.cmd, req_size);

    if (rxed_handle.is_valid()) {
        DEBUG_LOG("Unexpected handle in client request 0x%04x\n", req.hdr.cmd);
        return ERR_INVALID_ARGS;
    }

    switch (req.hdr.cmd) {
    case IHDA_CMD_GET_IDS: {
        if (req_size != sizeof(req.get_ids)) {
            DEBUG_LOG("Bad GET_IDS request length (%u != %zu)\n",
                    req_size, sizeof(req.get_ids));
            return ERR_INVALID_ARGS;
        }

        MX_DEBUG_ASSERT(pci_dev_ != nullptr);
        MX_DEBUG_ASSERT(regs_ != nullptr);

        ihda_get_ids_resp_t resp;
        mx_status_t res;
        if (((res = GetDevProperty(pci_dev_, BIND_PCI_VID, &resp.vid)) != NO_ERROR) ||
            ((res = GetDevProperty(pci_dev_, BIND_PCI_DID, &resp.did)) != NO_ERROR))
            return res;

        resp.hdr       = req.hdr;
        resp.ihda_vmaj = REG_RD(&regs_->vmaj);
        resp.ihda_vmin = REG_RD(&regs_->vmin);
        resp.rev_id    = 0;
        resp.step_id   = 0;

        return channel.Write(&resp, sizeof(resp));
    }

    case IHDA_CONTROLLER_CMD_SNAPSHOT_REGS:
        if (req_size != sizeof(req.snapshot_regs)) {
            DEBUG_LOG("Bad SNAPSHOT_REGS request length (%u != %zu)\n",
                    req_size, sizeof(req.snapshot_regs));
            return ERR_INVALID_ARGS;
        }

        return SnapshotRegs(channel, req.snapshot_regs);

    default:
        return ERR_INVALID_ARGS;
    }
}

mx_status_t IntelHDAController::DriverInit(mx_driver_t* driver) {
    // Note: It is assumed that calls to Init/Release are serialized by the
    // pci_dev manager.  If this assumption ever needs to be relaxed, explicit
    // serialization will need to be added here.
    if (driver_ != nullptr)
        return ERR_BAD_STATE;

    driver_ = driver;

    return NO_ERROR;
}

mx_status_t IntelHDAController::DriverBind(mx_driver_t* driver,
                                           mx_device_t* device,
                                           void** cookie) {
    if (cookie == nullptr) return ERR_INVALID_ARGS;
    if (driver != driver_) return ERR_INVALID_ARGS;

    AllocChecker ac;
    mxtl::RefPtr<IntelHDAController> controller;

    controller = mxtl::AdoptRef(new (&ac) IntelHDAController());
    if (!ac.check())
        return ERR_NO_MEMORY;

    // If we successfully initialize, transfer our reference into the unmanaged
    // world.  We will re-claim it later when unbind is called.
    mx_status_t ret = controller->Init(device);
    if (ret == NO_ERROR)
        *cookie = controller.leak_ref();

    return ret;
}

void IntelHDAController::DriverUnbind(mx_driver_t* driver,
                                      mx_device_t* device,
                                      void* cookie) {
    MX_DEBUG_ASSERT(cookie != nullptr);

    // Reclaim our reference from the cookie.
    auto controller =
        mxtl::internal::MakeRefPtrNoAdopt(reinterpret_cast<IntelHDAController*>(cookie));

    // Now let go of it.
    controller.reset();
}

mx_status_t IntelHDAController::DriverRelease(mx_driver_t* driver) {
    MX_DEBUG_ASSERT(driver == driver_);

    // If we are the last one out the door, turn off the lights in the thread pool.
    audio::DispatcherThread::ShutdownThreadPool();

    driver_ = nullptr;
    return NO_ERROR;
}

}  // namespace intel_hda
}  // namespace audio

extern "C" {
mx_status_t ihda_init_hook(mx_driver_t* driver) {
    return ::audio::intel_hda::IntelHDAController::DriverInit(driver);
}

mx_status_t ihda_bind_hook(mx_driver_t* driver, mx_device_t* pci_dev, void** cookie) {
    return ::audio::intel_hda::IntelHDAController::DriverBind(driver, pci_dev, cookie);
}

void ihda_unbind_hook(mx_driver_t* driver, mx_device_t* pci_dev, void* cookie) {
    ::audio::intel_hda::IntelHDAController::DriverUnbind(driver, pci_dev, cookie);
}

mx_status_t ihda_release_hook(mx_driver_t* driver) {
    return ::audio::intel_hda::IntelHDAController::DriverRelease(driver);
}
}  // extern "C"

