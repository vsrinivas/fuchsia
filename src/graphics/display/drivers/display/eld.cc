// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/display/eld.h"

#include <src/lib/eld/eld.h>

namespace display {

void ComputeEld(const edid::Edid& edid, fbl::Array<uint8_t>& eld) {
  // First we calculate the total length so we can allocate.
  // The total ELD length of the ELD includes the ELD header, the ELD baseline (parts 1, 2 and 3)
  // and the any vendor specific data (not suported).

  // We need the baseline part 2 variable length from the monitor name.
  // We populate up to kEldMonitorNameMaxLength bytes of monitor name.
  constexpr size_t kMaxMonitorNameStringLength = 16;
  size_t monitor_name_string_len = strlen(edid.monitor_name());
  if (monitor_name_string_len > kMaxMonitorNameStringLength) {
    monitor_name_string_len = kMaxMonitorNameStringLength;
  }
  const size_t eld_baseline_part2_length = monitor_name_string_len;

  // We need the number of short audio descriptors to calculate the baseline part 3 length.
  size_t number_of_short_audio_descriptors = 0;
  for (auto it = edid::audio_data_block_iterator(&edid); it.is_valid(); ++it) {
    if (it->format() != edid::ShortAudioDescriptor::kLPcm) {
      // TODO(andresoportus): Add compressed formats.
      continue;
    }
    number_of_short_audio_descriptors++;
  }
  const size_t eld_baseline_part3_length =
      number_of_short_audio_descriptors * sizeof(edid::ShortAudioDescriptor);

  // Now we can calculate the ELD length.
  size_t eld_length = sizeof(hda::EldHeader) + sizeof(hda::EldBaselinePart1) +
                      eld_baseline_part2_length + eld_baseline_part3_length;
  eld_length = (eld_length + 3) & ~3;  //  Make the ELD length multiple of 4.

  // With the ELD length we can allocate and then fill in the data.
  eld = fbl::Array<uint8_t>(new uint8_t[eld_length], eld_length);
  memset(eld.get(), 0, eld_length);  // Set reserved fields to 0.

  // Fill the data, moving pointer p along the way.
  uint8_t* p = eld.get();

  // Populate the ELD header.
  hda::EldHeader* header = reinterpret_cast<hda::EldHeader*>(p);
  header->set_eld_ver(2);
  header->set_baseline_eld_len(eld_length);
  p += sizeof(hda::EldHeader);

  // Populate the ELD baseline part 1.
  hda::EldBaselinePart1* part1 = reinterpret_cast<hda::EldBaselinePart1*>(p);
  // "with CEA-861-C and continuing through present, incrementing the version number is no longer
  // required. The revision number shall be set to 0x03"
  part1->set_cea_edid_ver(3);
  part1->set_mnl(monitor_name_string_len);
  part1->set_sad_count(number_of_short_audio_descriptors);
  part1->set_conn_type(edid.is_hdmi() ? 0 : 1);
  part1->set_s_ai(0);          // Not supported: ACP, ISRC1, or ISRC2 packets.
  part1->set_hdcp(0);          // Not supported.
  part1->aud_synch_delay = 0;  // Not supported.
  part1->byte4 = 0;            // Not supported: FLR, LFE, FC, RLR, RC, FLRC, RLRC.
  part1->port_id = 0;          // Not supported.
  part1->manufacturer_name = edid.manufacturer_name_code();
  part1->product_code = edid.product_code();
  p += sizeof(hda::EldBaselinePart1);

  // Populate the ELD baseline part 2: monitor_name_string.
  memcpy(p, edid.monitor_name(), monitor_name_string_len);
  p += monitor_name_string_len;

  // Populate the ELD baseline part 3: short audio descriptors.
  for (auto it = edid::audio_data_block_iterator(&edid); it.is_valid(); ++it) {
    if (it->format() != edid::ShortAudioDescriptor::kLPcm) {
      // TODO(andresoportus): Add compressed formats.
      continue;
    }
    edid::ShortAudioDescriptor* sad = reinterpret_cast<edid::ShortAudioDescriptor*>(p);
    p += sizeof(edid::ShortAudioDescriptor);
    *sad = *it;
  }
  // We don't populate the vendor specific block.
}

}  // namespace display
