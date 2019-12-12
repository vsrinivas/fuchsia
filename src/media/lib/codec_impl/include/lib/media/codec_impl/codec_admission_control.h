// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_ADMISSION_CONTROL_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_ADMISSION_CONTROL_H_

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <zircon/compiler.h>

#include <mutex>
#include <unordered_set>
#include <vector>

#include <fbl/macros.h>

// Controls how many Codec instances are concurrently served by this process.
//
// There's a limit of 1 for single-instance decoders, but arbitrarily-many
// multi-instance decoders can be used if there's no single-instance decoder.
class CodecAdmission;
class CodecAdmissionControl {
 public:
  // Create with a dispatcher to post async calls to the shared fidl thread.
  explicit CodecAdmissionControl(async_dispatcher_t* shared_fidl_dispatcher);

  // Get a move-only CodecAdmission as a move-only ticket that allows creation
  // of a CodecImpl.
  //
  // TODO(dustingreen): std::optional<> instead when C++17.
  void TryAddCodec(bool multi_instance, fit::function<void(std::unique_ptr<CodecAdmission>)>
                                            continue_after_previously_started_channel_closes_done);

  // Anything posted here will run after any previously-posted items here or via
  // TryAddCodec().
  //
  // Run the posted closure after all previously-started closes are done being
  // processed, and after all previously-queued closures via this method are
  // done.
  void PostAfterPreviouslyStartedClosesDone(fit::closure to_run);

 private:
  friend class CodecAdmission;

  // This keeps a set of deferred callbacks alive until all references to it are released.
  class PreviousCloseHandle {
   public:
    void AddClosureToReference(std::shared_ptr<fit::deferred_callback> input_defer) {
      defer_list_.push_back(input_defer);
    }

   private:
    std::vector<std::shared_ptr<fit::deferred_callback>> defer_list_;
  };

  // This is called after all previous closes are done.
  std::unique_ptr<CodecAdmission> TryAddCodecInternal(bool multi_instance);
  std::shared_ptr<PreviousCloseHandle> OnCodecIsClosing();

  void RemoveCodec(bool multi_instance);
  void CleanOutPreviousClosures() __TA_REQUIRES(lock_);

  async_dispatcher_t* shared_fidl_dispatcher_ = nullptr;

  // Must only be accessed from the shared fidl thread.
  std::unordered_set<CodecAdmission*> codecs_to_check_for_close_;

  std::mutex lock_;
  uint32_t single_instance_codec_count_ __TA_GUARDED(lock_) = 0;
  uint32_t multi_instance_codec_count_ __TA_GUARDED(lock_) = 0;

  std::vector<std::weak_ptr<PreviousCloseHandle>> previous_closes_ __TA_GUARDED(lock_);

  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecAdmissionControl);
};

class CodecAdmission {
 public:
  CodecAdmission(CodecAdmission&& from) = delete;
  CodecAdmission& operator=(CodecAdmission&& from) = delete;
  CodecAdmission(const CodecAdmission& from) = delete;
  CodecAdmission& operator=(const CodecAdmission& from) = delete;

  // Must be called from the shared fidl thread.
  void SetChannelToWaitOn(zx::unowned_channel channel) {
    channel_ = channel;
    if (*channel_) {
      codec_admission_control_->codecs_to_check_for_close_.insert(this);
    } else {
      codec_admission_control_->codecs_to_check_for_close_.erase(this);
    }
  }

  // Must be called from the shared fidl thread.
  void CheckIfChannelClosing();

  // Tell the codec admission control that this codec will be closing soon. When the class is
  // destroyed |close_handle_| will be destructed and that allows pending callbacks to run.
  void SetCodecIsClosing() {
    if (!close_handle_)
      close_handle_ = codec_admission_control_->OnCodecIsClosing();
  }

  ~CodecAdmission();

 private:
  friend class CodecAdmissionControl;
  CodecAdmission(CodecAdmissionControl* codec_admission_control, bool multi_instance);

  CodecAdmissionControl* codec_admission_control_ = nullptr;
  bool multi_instance_;
  std::shared_ptr<CodecAdmissionControl::PreviousCloseHandle> close_handle_;
  zx::unowned_channel channel_;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_ADMISSION_CONTROL_H_
