// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_VIM_SPDIF_AUDIO_STREAM_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_VIM_SPDIF_AUDIO_STREAM_H_

#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <zircon/device/audio.h>

#include <fbl/ref_ptr.h>

#include "vim-audio-utils.h"

// Fwd Decls
extern "C" {
struct vim2_display;
}

namespace audio {
namespace vim2 {

class Vim2SpdifAudioStream : public SimpleAudioStream {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Vim2SpdifAudioStream);
  static void Disable(const Registers& regs);

  uint64_t display_id() const { return display_id_; }

 protected:
  friend class fbl::RefPtr<Vim2SpdifAudioStream>;
  friend class SimpleAudioStream;

  Vim2SpdifAudioStream(const vim2_display* display, fbl::RefPtr<Registers> regs,
                       fbl::RefPtr<RefCountedVmo> ring_buffer_vmo,
                       fzl::PinnedVmo pinned_ring_buffer, uint64_t display_id);

  ~Vim2SpdifAudioStream() override { Shutdown(); }

  zx_status_t Init() __TA_REQUIRES(domain_token()) override;
  void ShutdownHook() __TA_REQUIRES(domain_token()) override;
  void RingBufferShutdown() __TA_REQUIRES(domain_token()) override;

  zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
      __TA_REQUIRES(domain_token()) override;
  zx_status_t SetGain(const audio_proto::SetGainReq& req) __TA_REQUIRES(domain_token()) override;

  zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req, uint32_t* out_num_rb_frames,
                        zx::vmo* out_buffer) __TA_REQUIRES(domain_token()) override;
  zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_token()) override;
  zx_status_t Stop() __TA_REQUIRES(domain_token()) override;

 private:
  zx_status_t CreateFormatList() __TA_REQUIRES(domain_token());

  void Enable();
  void SetupBuffer();
  void SetMode(uint32_t frame_rate, audio_sample_format_t fmt);
  void Mute(bool muted);

  // TODO(johngro) : it is unfortunate that we need to maintain an unmanaged
  // pointer back to our display in order to configure it properly when
  // setting audio modes.  In a perfect world, however, we would really not
  // know much of anything about us.  Instead, we would be able to properly
  // represent composite device drivers, and this audio code would be running
  // on its own in a separate devhost and acting as a DAI driver for various
  // codec drivers.  In this world, HDMI driver would serve as a codec driver,
  // and it would get first crack at the call to "set format", which would
  // allow it configure the audio clock recover and audio info-frame as part
  // of the process of requesting the proper DAI stream to feed the HDMI
  // transmitter unit in the chip.
  //
  // Until that day comes, however, we need a small callback hook into the
  // display driver to set this up when the high level code asks us to do so.
  // In order to do that, we need to hold the context pointer to the display
  // driver instance, which will be passed to us at construction time.  Since
  // we have no managed pointers, it is the HDMI driver's responsibility to
  // make certain that its scope outlives our.
  //
  const struct vim2_display* const display_;
  const uint64_t display_id_;

  fbl::RefPtr<Registers> regs_;
  fbl::RefPtr<RefCountedVmo> ring_buffer_vmo_;
  fzl::PinnedVmo pinned_ring_buffer_;
  uint32_t usable_buffer_size_ = 0;
};

}  // namespace vim2
}  // namespace audio

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_VIM_SPDIF_AUDIO_STREAM_H_
