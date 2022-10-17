// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/display/display-info.h"

#include <audio-proto-utils/format-utils.h>
#include <pretty/hexdump.h>

#include "src/devices/lib/audio/audio.h"

namespace display {

namespace {

struct I2cBus {
  ddk::I2cImplProtocolClient i2c;
  uint32_t bus_id;
};

edid::ddc_i2c_transact ddc_tx = [](void* ctx, edid::ddc_i2c_msg_t* msgs, uint32_t count) -> bool {
  auto i2c = static_cast<I2cBus*>(ctx);
  i2c_impl_op_t ops[count];
  for (unsigned i = 0; i < count; i++) {
    ops[i].address = msgs[i].addr;
    ops[i].data_buffer = msgs[i].buf;
    ops[i].data_size = msgs[i].length;
    ops[i].is_read = msgs[i].is_read;
    ops[i].stop = i == (count - 1);
  }
  return i2c->i2c.Transact(i2c->bus_id, ops, count) == ZX_OK;
};

}  // namespace

void DisplayInfo::InitializeInspect(inspect::Node* parent_node) {
  ZX_DEBUG_ASSERT(init_done);
  node = parent_node->CreateChild(fbl::StringPrintf("display-%lu", id).c_str());

  if (!edid.has_value()) {
    node.CreateUint("width", params.width, &properties);
    node.CreateUint("height", params.height, &properties);
    return;
  }

  size_t i = 0;
  for (const auto& t : edid->timings) {
    auto child = node.CreateChild(fbl::StringPrintf("timing-parameters-%lu", ++i).c_str());
    child.CreateDouble("vsync-hz", static_cast<double>(t.vertical_refresh_e2) / 100.0, &properties);
    child.CreateUint("pixel-clock-khz", t.pixel_freq_10khz * 10, &properties);
    child.CreateUint("horizontal-pixels", t.horizontal_addressable, &properties);
    child.CreateUint("horizontal-blanking", t.horizontal_blanking, &properties);
    child.CreateUint("horizontal-sync-offset", t.horizontal_front_porch, &properties);
    child.CreateUint("horizontal-sync-pulse", t.horizontal_sync_pulse, &properties);
    child.CreateUint("vertical-pixels", t.vertical_addressable, &properties);
    child.CreateUint("vertical-blanking", t.vertical_blanking, &properties);
    child.CreateUint("vertical-sync-offset", t.vertical_front_porch, &properties);
    child.CreateUint("vertical-sync-pulse", t.vertical_sync_pulse, &properties);
    properties.emplace(std::move(child));
  }
}

// static
zx::result<fbl::RefPtr<DisplayInfo>> DisplayInfo::Create(const added_display_args_t& info,
                                                         ddk::I2cImplProtocolClient* i2c) {
  fbl::AllocChecker ac;
  fbl::RefPtr<DisplayInfo> out = fbl::AdoptRef(new (&ac) DisplayInfo);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  out->pending_layer_change = false;
  out->vsync_layer_count = 0;
  out->id = info.display_id;
  if (info.edid_present) {
    out->edid = DisplayInfo::Edid{};
  }
  out->pixel_formats = fbl::Array<zx_pixel_format_t>(
      new (&ac) zx_pixel_format_t[info.pixel_format_count], info.pixel_format_count);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  out->cursor_infos = fbl::Array<cursor_info_t>(new (&ac) cursor_info_t[info.cursor_info_count],
                                                info.cursor_info_count);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  memcpy(out->pixel_formats.data(), info.pixel_format_list,
         out->pixel_formats.size() * sizeof(zx_pixel_format_t));
  memcpy(out->cursor_infos.data(), info.cursor_info_list,
         out->cursor_infos.size() * sizeof(cursor_info_t));
  if (!info.edid_present) {
    out->params = info.panel.params;
    return zx::ok(std::move(out));
  }

  if (!i2c->is_valid()) {
    zxlogf(ERROR, "Presented edid display with no i2c bus");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  bool success = false;
  const char* edid_err = "unknown error";

  uint32_t edid_attempt = 0;
  static constexpr uint32_t kEdidRetries = 3;
  do {
    if (edid_attempt != 0) {
      zxlogf(DEBUG, "Error %d/%d initializing edid: \"%s\"", edid_attempt, kEdidRetries, edid_err);
      zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    }
    edid_attempt++;

    I2cBus bus = {*i2c, info.panel.i2c_bus_id};
    success = out->edid->base.Init(&bus, ddc_tx, &edid_err);
  } while (!success && edid_attempt < kEdidRetries);

  if (!success) {
    zxlogf(INFO, "Failed to parse edid (%d bytes) \"%s\"", out->edid->base.edid_length(), edid_err);
    if (zxlog_level_enabled(INFO) && out->edid->base.edid_bytes()) {
      hexdump(out->edid->base.edid_bytes(), out->edid->base.edid_length());
    }
    return zx::error(ZX_ERR_INTERNAL);
  }

  out->PopulateDisplayAudio();

  if (zxlog_level_enabled(DEBUG) && out->edid->audio.size()) {
    zxlogf(DEBUG, "Supported audio formats:");
    for (auto range : out->edid->audio) {
      audio_stream_format_range temp_range;
      audio::audio_stream_format_fidl_from_banjo(range, &temp_range);
      for (auto rate : audio::utils::FrameRateEnumerator(temp_range)) {
        zxlogf(DEBUG, "  rate=%d, channels=[%d, %d], sample=%x", rate, range.min_channels,
               range.max_channels, range.sample_formats);
      }
    }
  }

  if (zxlog_level_enabled(DEBUG)) {
    const auto& edid = out->edid->base;
    const char* manufacturer =
        strlen(edid.manufacturer_name()) ? edid.manufacturer_name() : edid.manufacturer_id();
    zxlogf(DEBUG, "Manufacturer \"%s\", product %d, name \"%s\", serial \"%s\"", manufacturer,
           edid.product_code(), edid.monitor_name(), edid.monitor_serial());
    edid.Print([](const char* str) { zxlogf(DEBUG, "%s", str); });
  }
  return zx::ok(std::move(out));
}

void DisplayInfo::PopulateDisplayAudio() {
  fbl::AllocChecker ac;

  // Displays which support any audio are required to support basic
  // audio, so just bail if that bit isn't set.
  if (!edid->base.supports_basic_audio()) {
    return;
  }

  // TODO(fxbug.dev/32457): Revisit dedupe/merge logic once the audio API takes a stance. First,
  // this code always adds the basic audio formats before processing the SADs, which is likely
  // redundant on some hardware (the spec isn't clear about whether or not the basic audio formats
  // should also be included in the SADs). Second, this code assumes that the SADs are compact
  // and not redundant, which is not guaranteed.

  // Add the range for basic audio support.
  audio_types_audio_stream_format_range_t range;
  range.min_channels = 2;
  range.max_channels = 2;
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  range.min_frames_per_second = 32000;
  range.max_frames_per_second = 48000;
  range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY | ASF_RANGE_FLAG_FPS_44100_FAMILY;

  edid->audio.push_back(range, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory attempting to construct supported format list.");
    return;
  }

  for (auto it = edid::audio_data_block_iterator(&edid->base); it.is_valid(); ++it) {
    if (it->format() != edid::ShortAudioDescriptor::kLPcm) {
      // TODO(stevensd): Add compressed formats when audio format supports it
      continue;
    }
    audio_types_audio_stream_format_range_t range;

    constexpr audio_sample_format_t zero_format = static_cast<audio_sample_format_t>(0);
    range.sample_formats = static_cast<audio_sample_format_t>(
        (it->lpcm_24() ? AUDIO_SAMPLE_FORMAT_24BIT_PACKED | AUDIO_SAMPLE_FORMAT_24BIT_IN32
                       : zero_format) |
        (it->lpcm_20() ? AUDIO_SAMPLE_FORMAT_20BIT_PACKED | AUDIO_SAMPLE_FORMAT_20BIT_IN32
                       : zero_format) |
        (it->lpcm_16() ? AUDIO_SAMPLE_FORMAT_16BIT : zero_format));

    range.min_channels = 1;
    range.max_channels = static_cast<uint8_t>(it->num_channels_minus_1() + 1);

    // Now build continuous ranges of sample rates in the each family
    static constexpr struct {
      const uint32_t flag, val;
    } kRateLut[7] = {
        {edid::ShortAudioDescriptor::kHz32, 32000},   {edid::ShortAudioDescriptor::kHz44, 44100},
        {edid::ShortAudioDescriptor::kHz48, 48000},   {edid::ShortAudioDescriptor::kHz88, 88200},
        {edid::ShortAudioDescriptor::kHz96, 96000},   {edid::ShortAudioDescriptor::kHz176, 176400},
        {edid::ShortAudioDescriptor::kHz192, 192000},
    };

    for (uint32_t i = 0; i < std::size(kRateLut); ++i) {
      if (!(it->sampling_frequencies & kRateLut[i].flag)) {
        continue;
      }
      range.min_frames_per_second = kRateLut[i].val;

      if (audio::utils::FrameRateIn48kFamily(kRateLut[i].val)) {
        range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
      } else {
        range.flags = ASF_RANGE_FLAG_FPS_44100_FAMILY;
      }

      // We found the start of a range.  At this point, we are guaranteed
      // to add at least one new entry into the set of format ranges.
      // Find the end of this range.
      uint32_t j;
      for (j = i + 1; j < std::size(kRateLut); ++j) {
        if (!(it->bitrate & kRateLut[j].flag)) {
          break;
        }

        if (audio::utils::FrameRateIn48kFamily(kRateLut[j].val)) {
          range.flags |= ASF_RANGE_FLAG_FPS_48000_FAMILY;
        } else {
          range.flags |= ASF_RANGE_FLAG_FPS_44100_FAMILY;
        }
      }

      i = j - 1;
      range.max_frames_per_second = kRateLut[i].val;

      edid->audio.push_back(range, &ac);
      if (!ac.check()) {
        zxlogf(ERROR, "Out of memory attempting to construct supported format list.");
        return;
      }
    }
  }
}

}  // namespace display
