// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_AVCC_PROCESSOR_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_AVCC_PROCESSOR_H_

#include <lib/fit/function.h>
#include <lib/media/codec_impl/codec_adapter_events.h>
#include <lib/media/codec_impl/codec_input_item.h>

#include <vector>

// TODO(fxbug.dev/95162): Move to a centralized location.
class AvccProcessor {
 public:
  explicit AvccProcessor(fit::function<void(std::vector<uint8_t>)> decode_annex_b,
                         CodecAdapterEvents* codec_adapter_events)
      : decode_annex_b_(std::move(decode_annex_b)), events_(codec_adapter_events) {}

  void ProcessOobBytes(const fuchsia::media::FormatDetails& format_details);
  std::vector<uint8_t> ParseVideoAvcc(std::vector<uint8_t> input) const;

  bool is_avcc() const { return is_avcc_; }

 private:
  fit::function<void(std::vector<uint8_t>)> decode_annex_b_;
  CodecAdapterEvents* events_;
  bool is_avcc_ = false;
  uint32_t pseudo_nal_length_field_bytes_ = 0;
};

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_AVCC_PROCESSOR_H_
