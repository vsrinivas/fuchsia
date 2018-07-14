// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_PENDING_FLUSH_TOKEN_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_PENDING_FLUSH_TOKEN_H_

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>

namespace media {
namespace audio {

class AudioServerImpl;

class PendingFlushToken
    : public fbl::RefCounted<PendingFlushToken>,
      public fbl::Recyclable<PendingFlushToken>,
      public fbl::DoublyLinkedListable<fbl::unique_ptr<PendingFlushToken>> {
 public:
  static fbl::RefPtr<PendingFlushToken> Create(
      AudioServerImpl* const server,
      fuchsia::media::AudioRenderer2::FlushCallback callback) {
    return fbl::AdoptRef(new PendingFlushToken(server, std::move(callback)));
  }

  void Cleanup() { callback_(); }

 private:
  friend class fbl::RefPtr<PendingFlushToken>;
  friend class fbl::Recyclable<PendingFlushToken>;
  friend class fbl::unique_ptr<PendingFlushToken>;

  PendingFlushToken(AudioServerImpl* const server,
                    fuchsia::media::AudioRenderer2::FlushCallback callback)
      : server_(server), callback_(std::move(callback)) {}

  ~PendingFlushToken();

  void fbl_recycle();

  AudioServerImpl* const server_;
  fuchsia::media::AudioRenderer2::FlushCallback callback_;
  bool was_recycled_ = false;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_PENDING_FLUSH_TOKEN_H_
