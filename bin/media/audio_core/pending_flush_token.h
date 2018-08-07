// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_PENDING_FLUSH_TOKEN_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_PENDING_FLUSH_TOKEN_H_

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>

namespace media {
namespace audio {

class AudioCoreImpl;

class PendingFlushToken
    : public fbl::RefCounted<PendingFlushToken>,
      public fbl::Recyclable<PendingFlushToken>,
      public fbl::DoublyLinkedListable<fbl::unique_ptr<PendingFlushToken>> {
 public:
  static fbl::RefPtr<PendingFlushToken> Create(
      AudioCoreImpl* const service,
      fuchsia::media::AudioRenderer2::FlushCallback callback) {
    return fbl::AdoptRef(new PendingFlushToken(service, std::move(callback)));
  }

  void Cleanup() { callback_(); }

 private:
  friend class fbl::RefPtr<PendingFlushToken>;
  friend class fbl::Recyclable<PendingFlushToken>;
  friend class fbl::unique_ptr<PendingFlushToken>;

  PendingFlushToken(AudioCoreImpl* const service,
                    fuchsia::media::AudioRenderer2::FlushCallback callback)
      : service_(service), callback_(std::move(callback)) {}

  ~PendingFlushToken();

  void fbl_recycle();

  AudioCoreImpl* const service_;
  fuchsia::media::AudioRenderer2::FlushCallback callback_;
  bool was_recycled_ = false;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_PENDING_FLUSH_TOKEN_H_
