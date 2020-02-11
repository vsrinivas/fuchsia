// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim-spdif-audio-stream.h"

#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <soc/aml-s912/s912-audio.h>

#include <limits>
#include <numeric>
#include <utility>

#include "hdmitx.h"
#include "vim-display.h"

#define SHIFTED_MASK(_name) ((_name##_MASK) << (_name##_SHIFT))
#define SHIFTED_VAL(_name, _val) ((_val & _name##_MASK) << _name##_SHIFT)
#define MOD_FIELD(_name, _val) SHFTED_MASK(_name), SHIFTED_VAL(_name, _val)

namespace audio {
namespace vim2 {

namespace {
// 128 bytes per frame.  Why?  I have no idea.  This is clearly not an audio
// frame, nor is it a SPDIF block.  I suspect that it may be the amount of
// data which the DMA engine tries to fetch each time it jumps on the bus,
// but I don't really know for certain.
constexpr uint32_t AIU_958_BYTES_PER_FRAME = 128;

static const struct {
  uint32_t rate;
  uint32_t N;
} STANDARD_FRAME_RATE_N_LUT[] = {
    {.rate = 32000, .N = 4096},   {.rate = 48000, .N = 6144}, {.rate = 96000, .N = 12288},
    {.rate = 192000, .N = 25467}, {.rate = 44100, .N = 6272}, {.rate = 88200, .N = 12544},
    {.rate = 176400, .N = 28028},
};
}  // namespace

Vim2SpdifAudioStream::Vim2SpdifAudioStream(const vim2_display* display, fbl::RefPtr<Registers> regs,
                                           fbl::RefPtr<RefCountedVmo> ring_buffer_vmo,
                                           fzl::PinnedVmo pinned_ring_buffer, uint64_t display_id)
    : SimpleAudioStream(display->parent, false),
      display_(display),
      display_id_(display_id),
      regs_(std::move(regs)),
      ring_buffer_vmo_(std::move(ring_buffer_vmo)),
      pinned_ring_buffer_(std::move(pinned_ring_buffer)) {}

void Vim2SpdifAudioStream::ShutdownHook() {
  vim2_display_disable_audio(display_);
  Disable(*regs_);
}

void Vim2SpdifAudioStream::RingBufferShutdown() { vim2_display_disable_audio(display_); }

zx_status_t Vim2SpdifAudioStream::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  // Figure out the maximum number of audio frames we can fit into our ring
  // buffer while still guaranteeing...
  //
  // 1) The buffer is a multiple of audio frame size
  // 2) The buffer is a multiple of AIU frame size
  //
  ZX_DEBUG_ASSERT(frame_size_ > 0);
  usable_buffer_size_ = fbl::round_down(static_cast<uint32_t>(pinned_ring_buffer_.region(0).size),
                                        std::lcm(AIU_958_BYTES_PER_FRAME, frame_size_));

  // TODO(johngro): figure out the proper value for this
  fifo_depth_ = 512;

  // TODO(johngro): fill this out based on the estimate given by EDID (if any)
  external_delay_nsec_ = 0;

  // Figure out the proper values for N and CTS based on this audio mode and
  // pixel clock.
  // Start by going through our table of standard audio modes for standard
  // audio clocks.  If we cannot find the answer in the LUT, then fall back on
  // computing the answer on the fly using the recommended N as a starting
  // point to compute CTS.
  //
  // See section 7.2 (Audio Sample Clock Capture and Regeneration) of the HDMI
  // 1.3a spec (or later) for details.
  uint32_t N = 0;
  for (const auto& entry : STANDARD_FRAME_RATE_N_LUT) {
    if (entry.rate == req.frames_per_second) {
      N = entry.N;
      break;
    }
  }

  // This should never happen (As we are not advertising any frame rates which
  // are not in the LUT), but JiC.
  if (!N) {
    zxlogf(ERROR, "Failed to find starting N value for audio frame rate (%u).\n",
           req.frames_per_second);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Given our suggested starting value for N, CTS should be computed as...
  //
  // CTS = pixel_clock * N / (128 * audio_frame_rate)
  //
  // Since our pixel clock is already expressed in KHz, this becomes
  // CTS = pkhz * N * 1000 / (128 * audio_frame_rate)
  //     = pkhz * N * 125  / (16 * audio_frame_rate)
  //
  // If our numerator is not divisible by 16 * frame_rate, then we would (in
  // theory) need to dither the N/CTS values being sent, which is something we
  // currently do not support.  For now, if this happens, return an error
  // instead.
  uint64_t numer = static_cast<uint64_t>(display_->p->timings.pfreq) * N * 125;
  uint32_t denom = req.frames_per_second << 4;

  if (numer % denom) {
    zxlogf(ERROR, "Failed to find CTS value (pclk %u, N %u, frame_rate %u)\n",
           display_->p->timings.pfreq, N, req.frames_per_second);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t CTS = static_cast<uint32_t>(numer / denom);
  uint32_t bits_per_sample;
  switch (req.sample_format) {
    case AUDIO_SAMPLE_FORMAT_16BIT:
      bits_per_sample = 16;
      break;
    case AUDIO_SAMPLE_FORMAT_24BIT_PACKED:
      __FALLTHROUGH;
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:
      bits_per_sample = 24;
      break;
    default:
      zxlogf(ERROR, "Unsupported requested sample format (0x%08x)!\n", req.sample_format);
      return ZX_ERR_NOT_SUPPORTED;
  }

  // Set up the registers to match our format choice.
  SetMode(req.frames_per_second, req.sample_format);

  // Tell the HDMI driver about the mode we just configured.
  zx_status_t res;
  res = vim2_display_configure_audio_mode(display_, N, CTS, req.frames_per_second, bits_per_sample);
  if (res != ZX_OK) {
    zxlogf(ERROR, "Failed to configure VIM2 HDMI TX audio parameters! (res %d)\n", res);
    return res;
  }

  return ZX_OK;
}

zx_status_t Vim2SpdifAudioStream::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                            uint32_t* out_num_rb_frames, zx::vmo* out_buffer) {
  uint32_t rb_frames = usable_buffer_size_ / frame_size_;
  if (req.min_ring_buffer_frames > rb_frames) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
  zx_status_t res = ring_buffer_vmo_->vmo().duplicate(rights, out_buffer);
  if (res != ZX_OK) {
    return res;
  }

  *out_num_rb_frames = rb_frames;
  SetupBuffer();
  return ZX_OK;
}

zx_status_t Vim2SpdifAudioStream::Start(uint64_t* out_start_time) {
  uint64_t a, b;

  Mute(cur_gain_state_.cur_mute);
  a = zx_clock_get_monotonic();
  Enable();
  b = zx_clock_get_monotonic();
  *out_start_time = ((b - a) >> 1) + a;

  return ZX_OK;
}

zx_status_t Vim2SpdifAudioStream::Stop() {
  Disable(*regs_);
  Mute(false);
  return ZX_OK;
}

zx_status_t Vim2SpdifAudioStream::SetGain(const audio_proto::SetGainReq& req) {
  if (req.flags & AUDIO_SGF_MUTE_VALID) {
    cur_gain_state_.cur_mute = ((req.flags & AUDIO_SGF_MUTE) != 0);
    Mute(cur_gain_state_.cur_mute);
  }

  return ZX_OK;
}

zx_status_t Vim2SpdifAudioStream::Init() {
  zx_status_t res;

  if (!regs_) {
    zxlogf(ERROR, "null or invalid registers in %s\n", __PRETTY_FUNCTION__);
    return ZX_ERR_INVALID_ARGS;
  }

  Disable(*regs_);

  if (!ring_buffer_vmo_ || !ring_buffer_vmo_->vmo().is_valid()) {
    zxlogf(ERROR, "Bad ring buffer VMO passed to %s\n", __PRETTY_FUNCTION__);
    return ZX_ERR_INVALID_ARGS;
  }

  // Set up the DMA addresses.
  if ((pinned_ring_buffer_.region_count() != 1) ||
      (pinned_ring_buffer_.region(0).size < PAGE_SIZE) ||
      ((pinned_ring_buffer_.region(0).phys_addr + pinned_ring_buffer_.region(0).size) >=
       std::numeric_limits<uint32_t>::max())) {
    zxlogf(ERROR, "Bad ring buffer scatter/gather list passed to %s\n", __PRETTY_FUNCTION__);
    return ZX_ERR_INVALID_ARGS;
  }

  res = CreateFormatList();
  if (res != ZX_OK) {
    return res;
  }

  // Set our gain capabilities.
  cur_gain_state_.cur_gain = 0.0;
  cur_gain_state_.cur_mute = false;
  cur_gain_state_.cur_agc = false;

  cur_gain_state_.min_gain = 0.0;
  cur_gain_state_.max_gain = 0.0;
  cur_gain_state_.gain_step = 0.0;
  cur_gain_state_.can_mute = true;
  cur_gain_state_.can_agc = false;

  // Set our device node name.
  snprintf(device_name_, sizeof(device_name_), "vim2-spdif-out");

  // Create our unique ID by hashing portions of the EDID we get from our
  // display.  In particular, look for and hash...
  //
  // 1) The vendor/product ID.
  // 2) The first monitor descriptor, if present.
  // 3) The monitor serial number, if present.
  //
  // We deliberately do not simply hash contents the entire EDID.  Timing
  // and other configuration information can change, esp. when a device is
  // connected to an AV receiver and changes are made to the processing
  // configuration of the AVR.  We want to focus on attempting to identify the
  // device we are connected to, and not the mode that it is operating in.
  //
  // While we are parsing this information, also extract the manufacturer name
  // (from the vendor/product ID section), and the device name (from the first
  // monitor descriptor, if present).
  //
  // TODO(johngro): Someday, when this gets split into separate DAI/Codec
  // drivers, this code belongs in the HDMI codec section of things.
  digest::Digest sha;
  res = sha.Init();
  if (res != ZX_OK) {
    zxlogf(WARN, "Failed to initialize digest while computing unique ID.  (res %d)\n", res);
    return res;
  }

  // Seed our SHA with a constant number taken from 'uuidgen'.
  static const uint8_t SEED[] = {0xd8, 0x27, 0x52, 0xb7, 0x60, 0x9a, 0x46, 0xd4,
                                 0xa6, 0xc4, 0xdc, 0x32, 0xf5, 0xce, 0x1b, 0x7d};
  sha.Update(SEED, sizeof(SEED));

  snprintf(mfr_name_, sizeof(mfr_name_), "%s",
           strlen(display_->manufacturer_name) ? display_->manufacturer_name : "<unknown>");
  snprintf(prod_name_, sizeof(prod_name_), "%s",
           strlen(display_->monitor_name) ? display_->monitor_name : "Generic HDMI");

  sha.Update(mfr_name_, strnlen(mfr_name_, sizeof(mfr_name_)));
  sha.Update(prod_name_, strnlen(prod_name_, sizeof(prod_name_)));
  sha.Update(display_->monitor_serial,
             strnlen(display_->monitor_serial, sizeof(display_->monitor_serial)));

  // Finish the SHA and attempt to copy as much of the results to our internal
  // cached representation as we can.
  uint8_t digest_out[digest::kSha256Length];
  sha.Final();
  res = sha.CopyTo(digest_out, sizeof(digest_out));
  if (res != ZX_OK) {
    zxlogf(ERROR, "Failed to copy digest while computing unique ID.  (res %d)", res);
    return res;
  }
  ::memset(unique_id_.data, 0, sizeof(unique_id_.data));
  ::memcpy(unique_id_.data, digest_out, fbl::min(sizeof(digest_out), sizeof(unique_id_.data)));

  return ZX_OK;
}

void Vim2SpdifAudioStream::Disable(const Registers& regs) {
  regs.Write32(0, AIU_958_DCU_FF_CTRL);  // Disable the FIFO
  regs.ClearBits32(AIU_958_MCTRL_FILL_ENB | AIU_958_MCTRL_EMPTY_ENB,
                   AIU_MEM_IEC958_CONTROL);            // Disable the DMA
  regs.Write32(AIU_RS_958_FAST_DOMAIN, AIU_RST_SOFT);  // reset the unit
}

zx_status_t Vim2SpdifAudioStream::CreateFormatList() {
  // Compute the list of audio formats that we support.  To do this, we need
  // to intersect the capabilities of the display sink we are connect to, with
  // the capabilities of the S912 audio hardware.
  //
  // The DesignWare HDMI transmitter which is integrated into the S912 can be
  // fed a couple of different ways; either from one or more I2S units acting
  // in parallel, or one or more SPDIF units acting in parallel.  Each unit
  // can carry up to 2 channels of audio.  The DesignWare block also has
  // options to synthesize its own independent DMA engine (which would have
  // been super convenient), but these features were not enabled when the S912
  // was synthesized.
  //
  // The S912 has only 1 SPDIF unit (as well as only one I2S unit), which
  // limits our maximum number of channels to 2.
  //
  // In addition, the way that the clocks are being set up on VIM2, there is
  // no factor of 7 in the clock feeding the audio units.  Because of this, we
  // cannot generate any of the 44.1k family of audio rates.  We can, however,
  // generate clock rates up to 192KHz, and can generate 16, 20, and 24 bit audio.
  for (unsigned i = 0; i < display_->audio_format_count; i++) {
    audio_stream_format_range_t range;
    zx_status_t status = display_controller_interface_get_audio_format(
        &display_->dc_intf, display_->display_id, i, &range);
    ZX_ASSERT(status == ZX_OK);

    constexpr uint32_t SUPPORTED_FORMATS = AUDIO_SAMPLE_FORMAT_16BIT |
                                           AUDIO_SAMPLE_FORMAT_24BIT_PACKED |
                                           AUDIO_SAMPLE_FORMAT_24BIT_IN32;
    range.sample_formats =
        static_cast<audio_sample_format_t>(range.sample_formats & SUPPORTED_FORMATS);
    if (range.sample_formats == 0) {
      continue;
    }

    // Require stereo
    if (range.max_channels < 2) {
      continue;
    }
    range.max_channels = fbl::min<uint8_t>(range.max_channels, 2);

    constexpr uint32_t MIN_SUPPORTED_RATE = 32000;
    constexpr uint32_t MAX_SUPPORTED_RATE = 192000;
    range.flags &= ASF_RANGE_FLAG_FPS_48000_FAMILY;
    if (range.flags == 0 || range.max_frames_per_second < MIN_SUPPORTED_RATE ||
        range.min_frames_per_second > MAX_SUPPORTED_RATE) {
      continue;
    }
    range.max_frames_per_second = fbl::min(MAX_SUPPORTED_RATE, range.max_frames_per_second);
    range.min_frames_per_second = fbl::max(MIN_SUPPORTED_RATE, range.min_frames_per_second);

    fbl::AllocChecker ac;
    supported_formats_.push_back(range, &ac);
    if (!ac.check()) {
      zxlogf(ERROR, "Out of memory attempting to construct supported format list.\n");
      return ZX_ERR_NO_MEMORY;
    }
  }

  return ZX_OK;
}

void Vim2SpdifAudioStream::Enable() {
  const auto& regs = *regs_;

  regs.Write32(AIU_RS_958_FAST_DOMAIN, AIU_RST_SOFT);  // reset

  // Force the next sample fetched from the FIFO to be the start of a
  // frame by writing *any* value to the FORCE_LEFT register.
  //
  // Note: In the AmLogic documentation I have access to,  this register is
  // actually missing from the documentation (but mentioned briefly in the
  // discussion of bit 13 of AIU_958_MISC).  Notes left by the AM Logic driver
  // author in other codebases seem to say that when the SPDIF serializer has
  // been reset, that whether or not the next payload is supposed to be a left
  // or right sample does not actually get reset.  In order to get a proper
  // sequence of marker bits transmitted, we are supposed to use the
  // FORCE_LEFT register to reset this state as well any time we reset the
  // SPDIF TX unit.
  regs.Write32(0x00, AIU_958_FORCE_LEFT);

  regs.SetBits32(AIU_958_MCTRL_FILL_ENB | AIU_958_MCTRL_EMPTY_ENB,
                 AIU_MEM_IEC958_CONTROL);                        // Enable the DMA
  regs.SetBits32(AIU_958_DCU_FF_CTRL_ENB, AIU_958_DCU_FF_CTRL);  // Enable the fifo
}

void Vim2SpdifAudioStream::SetupBuffer() {
  ZX_DEBUG_ASSERT((regs_ != nullptr));
  const auto& regs = *regs_;

  // Set up the DMA addresses.
  ZX_DEBUG_ASSERT(pinned_ring_buffer_.region_count() == 1);
  ZX_DEBUG_ASSERT(pinned_ring_buffer_.region(0).size >= 8);
  ZX_DEBUG_ASSERT((pinned_ring_buffer_.region(0).phys_addr + pinned_ring_buffer_.region(0).size -
                   1) <= std::numeric_limits<uint32_t>::max());

  const auto& r = pinned_ring_buffer_.region(0);
  ZX_DEBUG_ASSERT(usable_buffer_size_ >= AIU_958_BYTES_PER_FRAME);
  ZX_DEBUG_ASSERT(usable_buffer_size_ <= r.size);
  regs.Write32(static_cast<uint32_t>(r.phys_addr), AIU_MEM_IEC958_START_PTR);
  regs.Write32(static_cast<uint32_t>(r.phys_addr), AIU_MEM_IEC958_RD_PTR);
  regs.Write32(static_cast<uint32_t>(r.phys_addr + usable_buffer_size_ - 8),
               AIU_MEM_IEC958_END_PTR);

  // Set the masks register to all channels present, and to read from all
  // channels.  Apparently, this is the thing to do when we are operating in
  // "split mode"
  regs.Write32(0xFFFF, AIU_MEM_IEC958_MASKS);

  // Now that the buffer has been set up, perform some register writes to the
  // CONTROL and BUF_CONTROL registers in order complete the setup.
  //
  // Exactly what this is accomplishing is something of a mystery.
  // Documentation for bit 0 of the MEM_CONTROL register consists of "bit 0:
  // cntl_init".  Documentation for the low 16 bits of the BUF_CNTL register
  // consists of "bits [0:15]: level_hold".  Why we need to follow this
  // sequence, or what it is accomplishing, is not documented.
  //
  // This sequence is here right now because it is done by the driver written
  // by AmLogic's engineer(s) in other code bases.  They provide no
  // real explanation for what is going on here either; so for now, this
  // remains nothing but cargo-cult garbage.
  regs.SetBits32(AIU_958_MCTRL_INIT, AIU_MEM_IEC958_CONTROL);
  regs.ClearBits32(AIU_958_MCTRL_INIT, AIU_MEM_IEC958_CONTROL);
  regs.Write32(1, AIU_MEM_IEC958_BUF_CNTL);
  regs.Write32(0, AIU_MEM_IEC958_BUF_CNTL);
}

void Vim2SpdifAudioStream::SetMode(uint32_t frame_rate, audio_sample_format_t fmt) {
  ZX_DEBUG_ASSERT((regs_ != nullptr));
  const auto& regs = *regs_;

  // Look up our frame rate to figure out our clock divider and channel status
  // bit.  Note: clock divider values are based on a reference frame rate of
  // 192KHz
  static const struct {
    uint32_t frame_rate;
    uint32_t div_bits;
    uint32_t ch_status_bits;
  } RATE_LUT[] = {
      {.frame_rate = 32000,
       .div_bits = SHIFTED_VAL(AIU_CLK_CTRL_958_DIV, 2u) | AIU_CLK_CTRL_958_DIV_MORE,
       .ch_status_bits = SPDIF_CS_SAMP_FREQ_32K},
      {.frame_rate = 48000,
       .div_bits = SHIFTED_VAL(AIU_CLK_CTRL_958_DIV, 3u),
       .ch_status_bits = SPDIF_CS_SAMP_FREQ_48K},
      {.frame_rate = 96000,
       .div_bits = SHIFTED_VAL(AIU_CLK_CTRL_958_DIV, 1u),
       .ch_status_bits = SPDIF_CS_SAMP_FREQ_96K},
      {.frame_rate = 192000,
       .div_bits = SHIFTED_VAL(AIU_CLK_CTRL_958_DIV, 0u),
       .ch_status_bits = SPDIF_CS_SAMP_FREQ_192K},
  };

  uint32_t rate_ndx;
  for (rate_ndx = 0; rate_ndx < fbl::count_of(RATE_LUT); ++rate_ndx) {
    if (RATE_LUT[rate_ndx].frame_rate == frame_rate) {
      break;
    }
  }

  // The requested frame rate should already have been validated by the code
  // before us.  If something has gone terribly wrong, log a warning and
  // default to 48K.
  if (rate_ndx >= fbl::count_of(RATE_LUT)) {
    constexpr uint32_t DEFAULT_RATE_NDX = 1;
    zxlogf(WARN, "Failed to find requested frame rate (%u) in LUT!  Defaulting to 48000\n",
           frame_rate);
    static_assert(DEFAULT_RATE_NDX < fbl::count_of(RATE_LUT), "Invalid default rate index!");
    rate_ndx = DEFAULT_RATE_NDX;
  }

  const auto& RATE = RATE_LUT[rate_ndx];

  // Now go ahead and set up the clock divider.
  constexpr uint32_t DIV_MASK = SHIFTED_MASK(AIU_CLK_CTRL_958_DIV) | AIU_CLK_CTRL_958_DIV_MORE;
  regs.ModifyBits32(RATE.div_bits, DIV_MASK, AIU_CLK_CTRL);

  // Send a 0 for the V bit in each frame.  This indicates that the audio is
  // "valid", at least from a PCM perspective.  When packing compressed audio
  // into a SPDIF transport, apparently the thing to do is set the V bit to 1
  // in order to prevent older SPDIF receivers from treating the data like PCM
  // and breaking your ears.
  regs.Write32(AIU_958_VCTRL_SEND_VBIT, AIU_958_VALID_CTRL);

  // TODO(johngro): Should the bytes per frame vary based on the size of an
  // audio frame?  In particular, should the bytes per frame be an integer
  // multiple of the audio frame size?
  regs.Write32(AIU_958_BYTES_PER_FRAME, AIU_958_BPF);

  // TODO(johngro): Provide some way to change the category code.  Shipping
  // products should not be sending "experimental" as their category code.
  constexpr uint32_t CH_STATUS_BASE = SPDIF_CS_SPDIF_CONSUMER | SPDIF_CS_AUD_DATA_PCM |
                                      SPDIF_CS_COPY_PERMITTED | SPDIF_CS_NO_PRE_EMPHASIS |
                                      SPDIF_CS_CCODE_EXPERIMENTAL | SPDIF_CS_CLK_ACC_100PPM;
  constexpr uint32_t MISC_BASE = AIU_958_MISC_FORCE_LR;
  constexpr uint32_t MCTRL_BASE = AIU_958_MCTRL_LINEAR_RAW | SHIFTED_VAL(AIU_958_MCTRL_ENDIAN, 0u);

  uint32_t ch_status = CH_STATUS_BASE | RATE.ch_status_bits;
  uint32_t misc = MISC_BASE;
  uint32_t mctrl = MCTRL_BASE;

  // TODO(johngro): Figure out how to get to bits >= 32 in the channel status
  // word.  In theory, we can use bits [32, 35] to signal the number of
  // significant bits in the encoding, as well as to indicate that the
  // auxiliary bits are carrying audio data instead of aux signalling.
  switch (fmt) {
    case AUDIO_SAMPLE_FORMAT_24BIT_PACKED:
      break;

    // Notes about the 32bit shift field.
    // The 958_MISC register has a 3-bit field in it whose documentation reads...
    //
    // "shift number for 32 bit mode"
    //
    // Experimentally, it has been determined that the SPDIF encoder expects
    // audio to be right justified when sending data from 32 bit containers.
    // IOW, if a user puts 24 bit samples into a 32 bit container, the SPDIF
    // encoder expects the samples to be in bits [0, 23].
    //
    // If audio is left justified instead (think 32 bit samples with the low
    // bits zeroed out), the "shift number" bits can be used.  The 32 bit words
    // will be right shifted by this number of bits for values [0, 6], or 8 bits
    // to the left when set to the 7.
    //
    // TL;DR?  When sending left justified audio in a 32 bit container, set this
    // field to 7.
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:
      misc |= AIU_958_MISC_32BIT_MODE | SHIFTED_VAL(AIU_958_MISC_32BIT_SHIFT, 7u);
      break;

    default:
      zxlogf(WARN, "Unsupported format (0x%08x), defaulting to PCM16\n", fmt);
      __FALLTHROUGH;
    case AUDIO_SAMPLE_FORMAT_16BIT:
      mctrl |= AIU_958_MCTRL_16BIT_MODE;
      misc |=
          AIU_958_MISC_16BIT | SHIFTED_VAL(AIU_958_MISC_16BIT_ALIGN, AIU_958_MISC_16BIT_ALIGN_LEFT);
      break;
  }

  regs.Write32((ch_status & 0xFFFF), AIU_958_CHSTAT_L0);
  regs.Write32((ch_status & 0xFFFF), AIU_958_CHSTAT_R0);
  regs.Write32((ch_status >> 16), AIU_958_CHSTAT_L1);
  regs.Write32((ch_status >> 16), AIU_958_CHSTAT_R1);
  regs.Write32(misc, AIU_958_MISC);
  regs.Write32(mctrl, AIU_MEM_IEC958_CONTROL);

  // Set the "level hold" to zero.  I have no idea why.
  regs.ClearBits32(SHIFTED_MASK(AIU_958_BCTRL_LEVEL_HOLD), AIU_MEM_IEC958_BUF_CNTL);
}

void Vim2SpdifAudioStream::Mute(bool muted) {
  constexpr uint32_t MUTE_BITS =
      AIU_958_CTRL_MUTE_LEFT | AIU_958_CTRL_MUTE_RIGHT | AIU_958_CTRL_FUB_ZERO;
  const auto& regs = *regs_;

  regs.Write32(muted ? MUTE_BITS : 0u, AIU_958_CTRL);
}

}  // namespace vim2
}  // namespace audio
