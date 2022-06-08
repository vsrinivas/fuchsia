// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_STREAM_BASE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_STREAM_BASE_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fuchsia/hardware/intelhda/codec/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/channel.h>

#include <utility>

#include <audio-proto/audio-proto.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/intel-hda-proto.h>

#include "channel.h"

namespace audio::intel_hda::codecs {

// Thread safety token.
//
// This token acts like a "no-op mutex", allowing compiler thread safety annotations
// to be placed on code or data that should only be accessed by a particular thread.
// Any code that acquires the token makes the claim that it is running on the (single)
// correct thread, and hence it is safe to access the annotated data and execute the annotated code.
struct __TA_CAPABILITY("role") Token {};
class __TA_SCOPED_CAPABILITY ScopedToken {
 public:
  explicit ScopedToken(const Token& token) __TA_ACQUIRE(token) {}
  ~ScopedToken() __TA_RELEASE() {}
};

class IntelHDACodecDriverBase;

class IntelHDAStreamBase;

class IntelHDAStreamBase : public fbl::RefCounted<IntelHDAStreamBase>,
                           public fbl::WAVLTreeContainable<fbl::RefPtr<IntelHDAStreamBase>> {
 public:
  zx_status_t Activate(fbl::RefPtr<IntelHDACodecDriverBase>&& parent_codec,
                       const fbl::RefPtr<Channel>& codec_channel) __TA_EXCLUDES(obj_lock_);

  void Deactivate() __TA_EXCLUDES(obj_lock_, default_domain_token());

  zx_status_t ProcessResponse(const CodecResponse& resp) __TA_EXCLUDES(obj_lock_);
  zx_status_t ProcessRequestStream(const ihda_proto::RequestStreamResp& resp)
      __TA_EXCLUDES(obj_lock_);
  virtual zx_status_t ProcessSetStreamFmtLocked(const ihda_proto::SetStreamFmtResp& resp)
      __TA_EXCLUDES(obj_lock_) = 0;

  uint32_t id() const { return id_; }
  bool is_input() const { return is_input_; }
  uint32_t GetKey() const { return id(); }

 protected:
  friend class fbl::RefPtr<IntelHDAStreamBase>;

  enum class Ack {
    NO,
    YES,
  };
  IntelHDAStreamBase(uint32_t id, bool is_input);
  virtual ~IntelHDAStreamBase();

  void SetPersistentUniqueId(const audio_stream_unique_id_t& id) __TA_EXCLUDES(obj_lock_);
  void SetPersistentUniqueIdLocked(const audio_stream_unique_id_t& id) __TA_REQUIRES(obj_lock_);
  audio_stream_unique_id_t& GetPersistentUniqueIdLocked() __TA_REQUIRES(obj_lock_) {
    return persistent_unique_id_;
  }
  void SetFormatChangeInProgress(bool in_progress) __TA_REQUIRES(obj_lock_) {
    format_change_in_progress_ = in_progress;
  }
  bool IsFormatChangeInProgress() const __TA_REQUIRES(obj_lock_) {
    return format_change_in_progress_;
  }

  // Overloads to control stream behavior.
  virtual zx_status_t OnActivateLocked() __TA_REQUIRES(obj_lock());
  virtual void OnDeactivateLocked() __TA_REQUIRES(obj_lock());
  virtual void OnDeactivate();
  virtual void RemoveDeviceLocked() __TA_REQUIRES(obj_lock());
  virtual zx_status_t OnDMAAssignedLocked() __TA_REQUIRES(obj_lock());
  virtual zx_status_t OnSolicitedResponseLocked(const CodecResponse& resp)
      __TA_REQUIRES(obj_lock());
  virtual zx_status_t OnUnsolicitedResponseLocked(const CodecResponse& resp)
      __TA_REQUIRES(obj_lock());
  virtual zx_status_t BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt)
      __TA_REQUIRES(obj_lock());
  virtual zx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt)
      __TA_REQUIRES(obj_lock());

  // Properties available to subclasses.
  uint8_t dma_stream_tag() const __TA_REQUIRES(obj_lock_) { return dma_stream_tag_; }

  const fbl::RefPtr<IntelHDACodecDriverBase>& parent_codec() const __TA_REQUIRES(obj_lock_) {
    return parent_codec_;
  }

  bool is_active() const __TA_REQUIRES(obj_lock_) { return parent_codec() != nullptr; }

  fbl::Mutex* obj_lock() __TA_RETURN_CAPABILITY(obj_lock_) { return &obj_lock_; }

  const Token& default_domain_token() const __TA_RETURN_CAPABILITY(domain_token_) {
    return domain_token_;
  }

  uint16_t encoded_fmt() const __TA_REQUIRES(obj_lock_) { return encoded_fmt_; }

  // Debug logging
  virtual void PrintDebugPrefix() const;

  zx_status_t SendCodecCommandLocked(uint16_t nid, CodecVerb verb, Ack do_ack)
      __TA_REQUIRES(obj_lock_);

  zx_status_t SendCodecCommand(uint16_t nid, CodecVerb verb, Ack do_ack) __TA_EXCLUDES(obj_lock_) {
    fbl::AutoLock obj_lock(&obj_lock_);
    return SendCodecCommandLocked(nid, verb, do_ack);
  }

  // Exposed to derived class for thread annotations.
  const fbl::Mutex& obj_lock() const __TA_RETURN_CAPABILITY(obj_lock_) { return obj_lock_; }

  // Unsolicited tag allocation for streams.
  zx_status_t AllocateUnsolTagLocked(uint8_t* out_tag) __TA_REQUIRES(obj_lock_);
  void ReleaseUnsolTagLocked(uint8_t tag) __TA_REQUIRES(obj_lock_);

  // Helper for derived classes implementing FIDL serving.
  zx_status_t CreateRingBufferLocked(
      fuchsia_hardware_audio::wire::Format format,
      fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer) __TA_REQUIRES(obj_lock_);

  zx_status_t SendSetStreamFmtLocked(uint16_t encoded_fmt, zx::handle ring_buffer_channel)
      __TA_REQUIRES(obj_lock());
  virtual zx_status_t PublishDeviceLocked() __TA_REQUIRES(obj_lock()) { return ZX_OK; }
  zx_status_t RecordPublishedDeviceLocked() __TA_REQUIRES(obj_lock());
  const char* dev_name() const { return dev_name_; }

 private:
  zx_status_t SetDMAStreamLocked(uint16_t id, uint8_t tag) __TA_REQUIRES(obj_lock());

  const uint32_t id_;
  const bool is_input_;
  char dev_name_[ZX_DEVICE_NAME_MAX] = {0};
  fbl::Mutex obj_lock_;

  fbl::RefPtr<IntelHDACodecDriverBase> parent_codec_ __TA_GUARDED(obj_lock_);
  fbl::RefPtr<Channel> codec_channel_ __TA_GUARDED(obj_lock_);

  uint16_t dma_stream_id_ __TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_ID;
  uint8_t dma_stream_tag_ __TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_TAG;

  zx_device_t* parent_device_ __TA_GUARDED(obj_lock_) = nullptr;

  uint16_t encoded_fmt_ __TA_GUARDED(obj_lock_);
  uint32_t unsol_tag_count_ __TA_GUARDED(obj_lock_) = 0;
  audio_stream_unique_id_t persistent_unique_id_;

  static zx_status_t EncodeStreamFormat(const audio_proto::StreamSetFmtReq& fmt,
                                        uint16_t* encoded_fmt_out);

  Token domain_token_;
  bool format_change_in_progress_ = false;
};

}  // namespace audio::intel_hda::codecs

#endif  // SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_STREAM_BASE_H_
