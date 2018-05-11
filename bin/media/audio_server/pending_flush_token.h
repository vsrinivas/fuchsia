// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <stdint.h>
#include <functional>

namespace media {
namespace audio {

class AudioServerImpl;

class PendingFlushToken
    : public fbl::RefCounted<PendingFlushToken>,
      public fbl::Recyclable<PendingFlushToken>,
      public fbl::DoublyLinkedListable<fbl::unique_ptr<PendingFlushToken>> {
 public:
  static fbl::RefPtr<PendingFlushToken> Create(
      AudioServerImpl* const server, const std::function<void()>& callback) {
    return fbl::AdoptRef(new PendingFlushToken(server, callback));
  }

  void Cleanup() { callback_(); }

 private:
  friend class fbl::RefPtr<PendingFlushToken>;
  friend class fbl::Recyclable<PendingFlushToken>;
  friend class fbl::unique_ptr<PendingFlushToken>;

  // TODO(johngro): Change the std::funciton here to an
  // AudioRenderer::FlushCallback once we have fully removed the V1 audio
  // renderer.
  PendingFlushToken(AudioServerImpl* const server,
                    const std::function<void()>& callback)
      : server_(server), callback_(callback) {}

  ~PendingFlushToken();

  void fbl_recycle();

  AudioServerImpl* const server_;
  std::function<void()> callback_;
  bool was_recycled_ = false;
};

}  // namespace audio
}  // namespace media
