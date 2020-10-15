// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-hda-controller.h"

#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <zircon/assert.h>

#include <atomic>
#include <utility>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/pci.h>
#include <dispatcher-pool/dispatcher-thread-pool.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <intel-hda/utils/intel-hda-proto.h>
#include <intel-hda/utils/intel-hda-registers.h>

#include "codec-connection.h"
#include "debug-logging.h"
#include "device-ids.h"
#include "src/media/audio/drivers/intel-hda/controller/intel_hda-bind.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

// static member variable declaration

std::atomic_uint32_t IntelHDAController::device_id_gen_(0u);

// Device FIDL thunks
fuchsia_hardware_intel_hda_ControllerDevice_ops_t IntelHDAController::CONTROLLER_FIDL_THUNKS = {
    .GetChannel =
        [](void* ctx, fidl_txn_t* txn) {
          return static_cast<IntelHDAController*>(ctx)->GetChannel(txn);
        },
};

// Device interface thunks
zx_protocol_device_t IntelHDAController::CONTROLLER_DEVICE_THUNKS = []() {
  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;
  ops.get_protocol = [](void* ctx, uint32_t proto_id, void* protocol) {
    return static_cast<IntelHDAController*>(ctx)->DeviceGetProtocol(proto_id, protocol);
  };
  ops.unbind = [](void* ctx) { static_cast<IntelHDAController*>(ctx)->DeviceShutdown(); };
  ops.release = [](void* ctx) { static_cast<IntelHDAController*>(ctx)->DeviceRelease(); };
  ops.message = [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_intel_hda_ControllerDevice_dispatch(
        ctx, txn, msg, &IntelHDAController::CONTROLLER_FIDL_THUNKS);
  };
  return ops;
}();

ihda_codec_protocol_ops_t IntelHDAController::CODEC_PROTO_THUNKS = {
    .get_driver_channel =
        [](void* ctx, zx_handle_t* channel_out) {
          return static_cast<IntelHDAController*>(ctx)->dsp_->CodecGetDispatcherChannel(
              channel_out);
        },
};

IntelHDAController::IntelHDAController()
    : state_(State::STARTING), id_(device_id_gen_.fetch_add(1u)) {
  snprintf(log_prefix_, sizeof(log_prefix_), "IHDA Controller (unknown BDF)");
}

IntelHDAController::~IntelHDAController() {
  ZX_DEBUG_ASSERT((GetState() == State::STARTING) || (GetState() == State::SHUT_DOWN));
  // TODO(johngro) : place the device into reset.

  // Release our register window.
  mapped_regs_.reset();

  // Release our IRQ.
  irq_.reset();

  // Disable IRQs at the PCI level.
  if (pci_.ops != nullptr) {
    ZX_DEBUG_ASSERT(pci_.ctx != nullptr);
    pci_.ops->set_irq_mode(pci_.ctx, ZX_PCIE_IRQ_MODE_DISABLED, 0);
  }

  // Let go of our stream state.
  free_input_streams_.clear();
  free_output_streams_.clear();
  free_bidir_streams_.clear();

  // Unmap, unpin and release the memory we use for the command/response ring buffers.
  cmd_buf_cpu_mem_.Unmap();
  cmd_buf_hda_mem_.Unpin();

  if (pci_.ops != nullptr) {
    // TODO(johngro) : unclaim the PCI device.  Right now, there is no way
    // to do this aside from closing the device handle (which would
    // seriously mess up the DevMgr's brain)
    pci_.ops = nullptr;
    pci_.ctx = nullptr;
  }
}

fbl::RefPtr<IntelHDAStream> IntelHDAController::AllocateStream(IntelHDAStream::Type type) {
  fbl::AutoLock lock(&stream_pool_lock_);
  IntelHDAStream::Tree* src;

  switch (type) {
    case IntelHDAStream::Type::INPUT:
      src = &free_input_streams_;
      break;
    case IntelHDAStream::Type::OUTPUT:
      src = &free_output_streams_;
      break;

    // Users are not allowed to directly request bidirectional stream contexts.
    // It's just what they end up with if there are no other choices.
    default:
      ZX_DEBUG_ASSERT(false);
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

void IntelHDAController::ReturnStream(fbl::RefPtr<IntelHDAStream>&& ptr) {
  fbl::AutoLock lock(&stream_pool_lock_);
  ReturnStreamLocked(std::move(ptr));
}

void IntelHDAController::ReturnStreamLocked(fbl::RefPtr<IntelHDAStream>&& ptr) {
  IntelHDAStream::Tree* dst;

  ZX_DEBUG_ASSERT(ptr);

  switch (ptr->type()) {
    case IntelHDAStream::Type::INPUT:
      dst = &free_input_streams_;
      break;
    case IntelHDAStream::Type::OUTPUT:
      dst = &free_output_streams_;
      break;
    case IntelHDAStream::Type::BIDIR:
      dst = &free_bidir_streams_;
      break;
    default:
      ZX_DEBUG_ASSERT(false);
      return;
  }

  ptr->Configure(IntelHDAStream::Type::INVALID, 0);
  dst->insert(std::move(ptr));
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

  ZX_DEBUG_ASSERT((tag > 0) && (tag <= 15));
  ZX_DEBUG_ASSERT((tag_pool & (1u << tag)) == 0);

  tag_pool = static_cast<uint16_t>((tag_pool | (1u << tag)));
}

zx_status_t IntelHDAController::DeviceGetProtocol(uint32_t proto_id, void* protocol) {
  switch (proto_id) {
    case ZX_PROTOCOL_IHDA_CODEC: {
      auto proto = static_cast<ihda_codec_protocol_t*>(protocol);
      proto->ops = &CODEC_PROTO_THUNKS;
      proto->ctx = this;
      return ZX_OK;
    }
    default:
      LOG(ERROR, "Unsupported protocol 0x%08x\n", proto_id);
      return ZX_ERR_NOT_SUPPORTED;
  }
}

void IntelHDAController::DeviceShutdown() {
  // Make sure we have closed all of the event sources (eg. IRQs, wakeup
  // events, channels clients are using to talk to us, etc..) and that we have
  // synchronized with any dispatch callbacks in flight.
  default_domain_->Deactivate();

  // Disable all interrupts and place the device into reset on our way out.
  if (regs() != nullptr) {
    REG_WR(&regs()->intctl, 0u);
    REG_CLR_BITS(&regs()->gctl, HDA_REG_GCTL_HWINIT);
  }

  // Shutdown and clean up all of our codecs.
  for (auto& codec_ptr : codecs_) {
    if (codec_ptr != nullptr) {
      codec_ptr->Shutdown();
      codec_ptr.reset();
    }
  }

  // Any CORB jobs we may have had in progress may be discarded.
  {
    fbl::AutoLock corb_lock(&corb_lock_);
    in_flight_corb_jobs_.clear();
    pending_corb_jobs_.clear();
  }

  // Done.  Clearly mark that we are now shut down.
  SetState(State::SHUT_DOWN);
}

void IntelHDAController::DeviceRelease() {
  // Take our unmanaged reference back from our published device node.
  auto thiz = fbl::ImportFromRawPtr(this);

  // ASSERT that we have been properly shut down, then release the DDK's
  // reference to our state as we allow thiz to go out of scope.
  ZX_DEBUG_ASSERT(GetState() == State::SHUT_DOWN);
  thiz.reset();
}

zx_status_t IntelHDAController::GetChannel(fidl_txn_t* txn) {
  dispatcher::Channel::ProcessHandler phandler(
      [controller = fbl::RefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, controller->default_domain_);
        return controller->ProcessClientRequest(channel);
      });

  zx::channel remote_endpoint_out;
  zx_status_t res = CreateAndActivateChannel(default_domain_, std::move(phandler), nullptr, nullptr,
                                             &remote_endpoint_out);

  if (res != ZX_OK) {
    return res;
  }

  return fuchsia_hardware_intel_hda_ControllerDeviceGetChannel_reply(txn,
                                                                     remote_endpoint_out.release());
}

void IntelHDAController::RootDeviceRelease() {
  // Take our unmanaged reference back from our published device node.
  auto thiz = fbl::ImportFromRawPtr(this);
  // Now let go of it.
  thiz.reset();
}

zx_status_t IntelHDAController::ProcessClientRequest(dispatcher::Channel* channel) {
  zx_status_t res;
  uint32_t req_size;
  union RequestBuffer {
    ihda_cmd_hdr_t hdr;
    ihda_get_ids_req_t get_ids;
    ihda_controller_snapshot_regs_req_t snapshot_regs;
  } req;

  // TODO(johngro) : How large is too large?
  static_assert(sizeof(req) <= 256, "Request buffer is too large to hold on the stack!");

  // Read the client request.
  ZX_DEBUG_ASSERT(channel != nullptr);
  res = channel->Read(&req, sizeof(req), &req_size);
  if (res != ZX_OK) {
    LOG(DEBUG, "Failed to read client request (res %d)\n", res);
    return res;
  }

  // Sanity checks
  if (req_size < sizeof(req.hdr)) {
    LOG(DEBUG, "Client request too small to contain header (%u < %zu)\n", req_size,
        sizeof(req.hdr));
    return ZX_ERR_INVALID_ARGS;
  }

  // Dispatch
  LOG(TRACE, "Client Request 0x%04x len %u\n", req.hdr.cmd, req_size);
  switch (req.hdr.cmd) {
    case IHDA_CMD_GET_IDS: {
      if (req_size != sizeof(req.get_ids)) {
        LOG(DEBUG, "Bad GET_IDS request length (%u != %zu)\n", req_size, sizeof(req.get_ids));
        return ZX_ERR_INVALID_ARGS;
      }

      ZX_DEBUG_ASSERT(pci_dev_ != nullptr);
      ZX_DEBUG_ASSERT(regs() != nullptr);

      ihda_get_ids_resp_t resp;
      resp.hdr = req.hdr;
      resp.vid = pci_dev_info_.vendor_id;
      resp.did = pci_dev_info_.device_id;
      resp.ihda_vmaj = REG_RD(&regs()->vmaj);
      resp.ihda_vmin = REG_RD(&regs()->vmin);
      resp.rev_id = 0;
      resp.step_id = 0;

      return channel->Write(&resp, sizeof(resp));
    }

    case IHDA_CONTROLLER_CMD_SNAPSHOT_REGS:
      if (req_size != sizeof(req.snapshot_regs)) {
        LOG(DEBUG, "Bad SNAPSHOT_REGS request length (%u != %zu)\n", req_size,
            sizeof(req.snapshot_regs));
        return ZX_ERR_INVALID_ARGS;
      }

      return SnapshotRegs(channel, req.snapshot_regs);

    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_protocol_device_t IntelHDAController::ROOT_DEVICE_THUNKS = []() {
  zx_protocol_device_t proto = {};
  proto.version = DEVICE_OPS_VERSION;
  proto.release = [](void* ctx) { static_cast<IntelHDAController*>(ctx)->RootDeviceRelease(); };
  return proto;
}();

zx_status_t IntelHDAController::DriverBind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  fbl::RefPtr<IntelHDAController> controller(fbl::AdoptRef(new (&ac) IntelHDAController()));

  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t ret = controller->Init(device);
  if (ret != ZX_OK) {
    return ret;
  }

  // Initialize our device and fill out the protocol hooks
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "intel-hda-controller";
  {
    // use a different refptr to avoid problems in error path
    auto ddk_ref = controller;
    args.ctx = fbl::ExportToRawPtr(&ddk_ref);
  }
  args.ops = &ROOT_DEVICE_THUNKS;
  args.flags = DEVICE_ADD_NON_BINDABLE;

  // Publish the device.
  ret = device_add(device, &args, nullptr);
  if (ret != ZX_OK) {
    controller.reset();
  }
  return ret;
}

void IntelHDAController::DriverRelease(void* ctx) {
  // If we are the last one out the door, turn off the lights in the thread pool.
  dispatcher::ThreadPool::ShutdownAll();
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = IntelHDAController::DriverBind;
  ops.release = IntelHDAController::DriverRelease;
  return ops;
}();

}  // namespace intel_hda
}  // namespace audio

ZIRCON_DRIVER(intel_hda, audio::intel_hda::driver_ops, "zircon", "0.1")
