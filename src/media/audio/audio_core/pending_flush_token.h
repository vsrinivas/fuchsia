// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PENDING_FLUSH_TOKEN_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PENDING_FLUSH_TOKEN_H_

#include <fuchsia/media/cpp/fidl.h>

#include <memory>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace media::audio {

class AudioCoreImpl;

class PendingFlushToken : public fbl::RefCounted<PendingFlushToken>,
                          public fbl::Recyclable<PendingFlushToken>,
                          public fbl::DoublyLinkedListable<std::unique_ptr<PendingFlushToken>> {
 public:
  static fbl::RefPtr<PendingFlushToken> Create(
      async_dispatcher_t* dispatcher,
      fuchsia::media::AudioRenderer::DiscardAllPacketsCallback callback) {
    return fbl::AdoptRef(new PendingFlushToken(dispatcher, std::move(callback)));
  }

 private:
  friend class fbl::RefPtr<PendingFlushToken>;
  friend class fbl::Recyclable<PendingFlushToken>;
  friend class std::default_delete<PendingFlushToken>;

  PendingFlushToken(async_dispatcher_t* dispatcher,
                    fuchsia::media::AudioRenderer::DiscardAllPacketsCallback callback)
      : dispatcher_(dispatcher), callback_(std::move(callback)) {}

  ~PendingFlushToken() = default;

  void fbl_recycle();

  async_dispatcher_t* dispatcher_;
  fuchsia::media::AudioRenderer::DiscardAllPacketsCallback callback_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PENDING_FLUSH_TOKEN_H_
