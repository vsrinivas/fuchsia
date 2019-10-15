// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim-audio.h"

#include <math.h>

#include <limits>
#include <new>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "hdmitx.h"
#include "vim-display.h"

namespace audio {
namespace vim2 {

// Create a ring buffer large enough to hold 1 second of 48kHz stereo 16-bit
// audio.
//
// TODO(johngro): Look into what it would take to remove the restriction that
// this buffer be contiguous so that we can more easily map the buffer on the
// fly without needing to take precious contiguous memory.
constexpr size_t SPDIF_RB_SIZE = fbl::round_up<size_t, size_t>(48000 * 2 * 2u, PAGE_SIZE);

Vim2Audio::~Vim2Audio() {}

zx_status_t Vim2Audio::Init(const pdev_protocol_t* pdev) {
  zx_status_t res;

  // Get a hold of our registers.
  regs_ = Registers::Create(pdev, MMIO_AUD_OUT, &res);
  if (res != ZX_OK) {
    DISP_ERROR("Error mapping registers (mmio_id %u, res %d)\n", MMIO_AUD_OUT, res);
    return res;
  }
  ZX_DEBUG_ASSERT(regs_ != nullptr);

  // Place the various units into reset
  //
  // TODO(johngro): Add I2S to this list, right now we are only managing SPDIF
  Vim2SpdifAudioStream::Disable(*regs_);

  // Obtain our BTI from the platform manager
  res = pdev_get_bti(pdev, 0, audio_bti_.reset_and_get_address());
  if (res != ZX_OK) {
    DISP_ERROR("Failed to get audio BTI handle! (res = %d)\n", res);
    return res;
  }

  // Now that we have our BTI, and we have quiesced our hardware, we can
  // release any quarantined VMOs which may be lingering from a previous
  // crash.  Note, it should be impossible for this to fail.
  res = audio_bti_.release_quarantine();
  ZX_DEBUG_ASSERT(res == ZX_OK);

  // Allocate the buffer we will use for SPDIF
  //
  // TODO(johngro): How do we guarantee that this memory's phys location is
  // below the 4GB mark?
  zx::vmo spdif_rb_vmo;
  res = zx_vmo_create_contiguous(audio_bti_.get(), SPDIF_RB_SIZE, 0,
                                 spdif_rb_vmo.reset_and_get_address());
  if (res != ZX_OK) {
    DISP_ERROR("Failed to allocate %zu byte ring buffer! (res = %d)\n", SPDIF_RB_SIZE, res);
    return res;
  }

  spdif_rb_vmo_ = RefCountedVmo::Create(std::move(spdif_rb_vmo));
  if (spdif_rb_vmo_ == nullptr) {
    DISP_ERROR("Failed to allocate RefCountedVmo\n");
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

void Vim2Audio::OnDisplayAdded(const vim2_display_t* display, uint64_t display_id) {
  if (spdif_stream_ != nullptr) {
    ZX_DEBUG_ASSERT(spdif_stream_->display_id() != display_id);
    return;
  }

  if (!display->p) {
    zxlogf(WARN, "HDMI parameters are not set up.  Cannot enable audio!\n");
    return;
  }

  // Pin our VMO so that HW can access it.
  fzl::PinnedVmo pinned_spdif_rb;
  zx_status_t res;
  res = pinned_spdif_rb.Pin(spdif_rb_vmo_->vmo(), audio_bti_, ZX_VM_PERM_READ);
  if (res != ZX_OK) {
    DISP_ERROR("Failed to pin %zu byte ring buffer! (res = %d)\n", SPDIF_RB_SIZE, res);
    return;
  }

  // Sanity check the pinned VMO.
  if (pinned_spdif_rb.region_count() != 1) {
    DISP_ERROR("Audio ring buffer VMO is not contiguous! (regions = %u)\n",
               pinned_spdif_rb.region_count());
    return;
  }

  const auto& r = pinned_spdif_rb.region(0);
  if ((r.phys_addr + r.size - 1) > std::numeric_limits<uint32_t>::max()) {
    DISP_ERROR("Audio ring buffer VMO is not below 4GB! [0x%zx, 0x%zx]\n", r.phys_addr,
               r.phys_addr + r.size);
    return;
  }

  spdif_stream_ = SimpleAudioStream::Create<Vim2SpdifAudioStream>(
      display, regs_, spdif_rb_vmo_, std::move(pinned_spdif_rb), display_id);
}

void Vim2Audio::OnDisplayRemoved(uint64_t display_id) {
  if (spdif_stream_ && (spdif_stream_->display_id() == display_id)) {
    spdif_stream_->Shutdown();
    spdif_stream_ = nullptr;
  }
}

}  // namespace vim2
}  // namespace audio

extern "C" {

zx_status_t vim2_audio_create(const pdev_protocol_t* pdev, vim2_audio_t** out_audio) {
  ZX_DEBUG_ASSERT(pdev != nullptr);
  ZX_DEBUG_ASSERT(out_audio != nullptr);
  *out_audio = nullptr;

  if (*out_audio != nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  fbl::AllocChecker ac;
  auto audio = fbl::make_unique_checked<audio::vim2::Vim2Audio>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t res = audio->Init(pdev);
  if (res != ZX_OK) {
    return res;
  }

  *out_audio = reinterpret_cast<vim2_audio_t*>(audio.release());
  return ZX_OK;
}

void vim2_audio_shutdown(vim2_audio_t** inout_audio) {
  ZX_DEBUG_ASSERT(inout_audio);
  delete reinterpret_cast<audio::vim2::Vim2Audio*>(*inout_audio);
  *inout_audio = nullptr;
}

void vim2_audio_on_display_added(const vim2_display_t* display, uint64_t display_id) {
  if (!display->audio) {
    zxlogf(WARN, "Failed to add audio stream; missing Vim2Audio instance!\n");
    return;
  }

  auto cpp_audio = reinterpret_cast<audio::vim2::Vim2Audio*>(display->audio);
  cpp_audio->OnDisplayAdded(display, display_id);
}

void vim2_audio_on_display_removed(const vim2_display_t* display, uint64_t display_id) {
  if (!display->audio) {
    zxlogf(WARN, "Failed to add audio stream; missing Vim2Audio instance!\n");
    return;
  }

  auto cpp_audio = reinterpret_cast<audio::vim2::Vim2Audio*>(display->audio);
  cpp_audio->OnDisplayRemoved(display_id);
}

}  // extern "C"
