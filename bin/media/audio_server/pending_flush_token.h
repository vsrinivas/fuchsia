// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_PENDING_FLUSH_TOKEN_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_PENDING_FLUSH_TOKEN_H_

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/fit/function.h>
#include <stdint.h>

namespace media {
namespace audio {

class AudioServerImpl;

class PendingFlushToken
    : public fbl::RefCounted<PendingFlushToken>,
      public fbl::Recyclable<PendingFlushToken>,
      public fbl::DoublyLinkedListable<fbl::unique_ptr<PendingFlushToken>> {
 public:
  static fbl::RefPtr<PendingFlushToken> Create(AudioServerImpl* const server,
                                               fit::closure callback) {
    return fbl::AdoptRef(new PendingFlushToken(server, std::move(callback)));
  }

  void Cleanup() { callback_(); }

 private:
  friend class fbl::RefPtr<PendingFlushToken>;
  friend class fbl::Recyclable<PendingFlushToken>;
  friend class fbl::unique_ptr<PendingFlushToken>;

  // TODO(johngro): Change the fit::closure here to an
  // AudioRenderer::FlushCallback once we have fully removed the V1 audio
  // renderer.
  PendingFlushToken(AudioServerImpl* const server, fit::closure callback)
      : server_(server), callback_(std::move(callback)) {}

  ~PendingFlushToken();

  void fbl_recycle();

  AudioServerImpl* const server_;
  fit::closure callback_;
  bool was_recycled_ = false;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_PENDING_FLUSH_TOKEN_H_
