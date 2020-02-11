// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <fuchsia/hardware/audio/llcpp/fidl.h>

#include <atomic>
#include <utility>

#include "a113-ddr.h"
#include "a113-pdm.h"
#include "audio-proto/audio-proto.h"
#include "dispatcher-pool/dispatcher-channel.h"
#include "dispatcher-pool/dispatcher-execution-domain.h"

#include "a113-ddr.h"
#include "a113-pdm.h"
#include "vmo_helper.h"

namespace audio {
namespace gauss {

class GaussPdmInputStream;
using GaussPdmInputStreamBase = ddk::Device<GaussPdmInputStream, ddk::Messageable,
                                            ddk::UnbindableNew>;

class GaussPdmInputStream : public GaussPdmInputStreamBase,
                            public ddk::EmptyProtocol<ZX_PROTOCOL_AUDIO_INPUT>,
                            public fbl::RefCounted<GaussPdmInputStream>,
                            public ::llcpp::fuchsia::hardware::audio::Device::Interface {
 public:
  static zx_status_t Create(zx_device_t* parent);

  // DDK device implementation
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    llcpp::fuchsia::hardware::audio::Device::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

 private:
  friend class fbl::RefPtr<GaussPdmInputStream>;

  GaussPdmInputStream(zx_device_t* parent,
                      fbl::RefPtr<dispatcher::ExecutionDomain>&& default_domain)
      : GaussPdmInputStreamBase(parent), default_domain_(std::move(default_domain)) {}

  virtual ~GaussPdmInputStream();

  // Device FIDL implementation
  void GetChannel(GetChannelCompleter::Sync completer) override;

  int IrqThread();

  zx_status_t Bind(const char* devname, zx_device_t* parent);

  // Thunks for dispatching stream channel events.
  zx_status_t ProcessStreamChannel(dispatcher::Channel* channel, bool privileged);

  void DeactivateStreamChannel(const dispatcher::Channel* channel);

  zx_status_t OnGetStreamFormats(dispatcher::Channel* channel,
                                 const audio_proto::StreamGetFmtsReq& req);

  zx_status_t OnSetStreamFormat(dispatcher::Channel* channel,
                                const audio_proto::StreamSetFmtReq& req, bool privileged);

  zx_status_t OnGetGain(dispatcher::Channel* channel, const audio_proto::GetGainReq& req);

  zx_status_t OnSetGain(dispatcher::Channel* channel, const audio_proto::SetGainReq& req);

  zx_status_t OnPlugDetect(dispatcher::Channel* channel, const audio_proto::PlugDetectReq& req);

  zx_status_t OnGetUniqueId(dispatcher::Channel* channel, const audio_proto::GetUniqueIdReq& req);
  zx_status_t OnGetString(dispatcher::Channel* channel, const audio_proto::GetStringReq& req);

  // Thunks for dispatching ring buffer channel events.
  zx_status_t ProcessRingBufferChannel(dispatcher::Channel* channel);

  void DeactivateRingBufferChannel(const dispatcher::Channel* channel);

  // Stream command handlers
  // Ring buffer command handlers
  zx_status_t OnGetFifoDepth(dispatcher::Channel* channel,
                             const audio_proto::RingBufGetFifoDepthReq& req) __TA_REQUIRES(lock_);

  zx_status_t OnGetBuffer(dispatcher::Channel* channel, const audio_proto::RingBufGetBufferReq& req)
      __TA_REQUIRES(lock_);

  zx_status_t OnStart(dispatcher::Channel* channel, const audio_proto::RingBufStartReq& req)
      __TA_REQUIRES(lock_);

  zx_status_t OnStop(dispatcher::Channel* channel, const audio_proto::RingBufStopReq& req)
      __TA_REQUIRES(lock_);

  fbl::Mutex lock_;

  // Dispatcher framework state
  fbl::RefPtr<dispatcher::Channel> stream_channel_;
  fbl::RefPtr<dispatcher::Channel> rb_channel_ __TA_GUARDED(lock_);
  fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

  fbl::Vector<audio_stream_format_range_t> supported_formats_;

  uint32_t frame_size_;

  VmoHelper<false> vmo_helper_;

  // TODO(almasrymina): hardcoded.
  uint32_t frame_rate_ = 48000;

  a113_audio_device_t audio_device_;
  thrd_t irqthrd_;

  uint32_t fifo_depth_ = 0x200;

  std::atomic<size_t> ring_buffer_size_;
  std::atomic<uint32_t> notifications_per_ring_;
};

}  // namespace gauss
}  // namespace audio
