// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_CHANNEL_H_
#define SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_CHANNEL_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>

namespace audio::intel_hda {

class Channel : public fbl::RefCounted<Channel> {
 public:
  template <typename... ConstructorSignature>
  static fbl::RefPtr<Channel> Create(ConstructorSignature&&... args) {
    fbl::AllocChecker ac;
    auto ptr = fbl::AdoptRef(new (&ac) Channel(std::forward<ConstructorSignature>(args)...));

    if (!ac.check()) {
      return nullptr;
    }

    return ptr;
  }

  void SetHandler(async::Wait::Handler handler) { wait_.set_handler(std::move(handler)); }
  zx_status_t BeginWait(async_dispatcher_t* dispatcher) { return wait_.Begin(dispatcher); }
  zx_status_t Write(const void* buffer, uint32_t length) {
    return channel_.write(0, buffer, length, nullptr, 0);
  }
  zx_status_t Write(const void* buffer, uint32_t length, zx::handle handle) {
    if (handle.is_valid()) {
      zx_handle_t h = handle.release();
      return channel_.write(0, buffer, length, &h, 1);
    } else {
      return Write(buffer, length);
    }
  }
  zx_status_t Read(void* buffer, uint32_t length, uint32_t* out_length) {
    return channel_.read(0, buffer, nullptr, length, 0, out_length, nullptr);
  }
  zx_status_t Read(void* buffer, uint32_t length, uint32_t* out_length, zx::handle& handle) {
    return channel_.read(0, buffer, handle.reset_and_get_address(), length, 1, out_length, nullptr);
  }

 protected:
  explicit Channel(zx::channel channel) : channel_(std::move(channel)) {
    wait_.set_object(channel_.get());
    wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  }
  ~Channel() = default;  // Deactivates (automatically cancels the wait from its RAII semantics).

 private:
  friend class fbl::RefPtr<Channel>;

  zx::channel channel_;
  async::Wait wait_;
};

class RingBufferChannel : public fbl::RefCounted<RingBufferChannel> {
 public:
  template <typename... ConstructorSignature>
  static fbl::RefPtr<RingBufferChannel> Create(ConstructorSignature&&... args) {
    fbl::AllocChecker ac;
    auto ptr =
        fbl::AdoptRef(new (&ac) RingBufferChannel(std::forward<ConstructorSignature>(args)...));

    if (!ac.check()) {
      return nullptr;
    }

    return ptr;
  }
};

}  // namespace audio::intel_hda

#endif  // SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_CHANNEL_H_
