// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <limits>
#include <utility>
#include <vector>

#include <conformance/cpp/libfuzzer_decode_encode.h>
#include <fuzzer/FuzzedDataProvider.h>

bool checkSizeToUint32(size_t size) { return size <= std::numeric_limits<uint32_t>::max(); }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  // Test oversized, but not ludicrously sized, collection of handles.
  auto num_handles = provider.ConsumeIntegralInRange<uint64_t>(0, 2 * ZX_CHANNEL_MAX_MSG_HANDLES);

  std::vector<zx_handle_info_t> handle_infos;
  for (uint64_t i = 0; i < num_handles; i++) {
    zx_handle_info_t handle_info;
    handle_info.type = provider.ConsumeIntegral<uint32_t>();
    // TODO(markdittmer): Use interesting handle rights and values. This may require a change in
    // corpus data format.
    handle_info.rights = 0;
    handle_info.handle = ZX_HANDLE_INVALID;
    handle_infos.push_back(handle_info);
  }
  if (!checkSizeToUint32(handle_infos.size()))
    return 0;

  std::vector<uint8_t> message_bytes = provider.ConsumeRemainingBytes<uint8_t>();
  if (!checkSizeToUint32(message_bytes.size()))
    return 0;

  for (auto decoder_encoder : fuzzing::conformance_decoder_encoders) {
    // Result is unused on builds with assertions disabled.
    [[maybe_unused]] auto decode_encode_status =
        decoder_encoder(message_bytes.data(), static_cast<uint32_t>(message_bytes.size()),
                        handle_infos.data(), static_cast<uint32_t>(handle_infos.size()));

    // When decode succeeds, (re-)encode should also succeed.
    assert(decode_encode_status.first != ZX_OK || decode_encode_status.second == ZX_OK);
  }

  return 0;
}
