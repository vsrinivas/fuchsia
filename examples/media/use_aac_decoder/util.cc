// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <memory>

void Exit(const char* format, ...) {
  // Let's not have a buffer on the stack, not because it couldn't be done
  // safely, but because we'd potentially run into stack size vs. message length
  // tradeoffs, stack expansion granularity fun, or whatever else.

  va_list args;
  va_start(args, format);
  size_t buffer_bytes = vsnprintf(nullptr, 0, format, args) + 1;
  va_end(args);

  // ~buffer never actually runs since this method never returns
  std::unique_ptr<char[]> buffer(new char[buffer_bytes]);

  va_start(args, format);
  size_t buffer_bytes_2 =
      vsnprintf(buffer.get(), buffer_bytes, format, args) + 1;
  (void)buffer_bytes_2;
  // sanity check; should match so go ahead and assert that it does.
  assert(buffer_bytes == buffer_bytes_2);
  va_end(args);

  printf("%s - exiting\n", buffer.get());

  // If anything goes wrong, exit(-1) is used directly (until we have any reason
  // to do otherwise).
  exit(-1);
}

// This is obviously not how anyone would really stream a file, but this example
// program isn't about streaming a large media file.
std::unique_ptr<uint8_t[]> read_whole_file(const char* filename, size_t* size) {
  std::ifstream file;
  // std::ios::ate means start at the end to tellg() will get the size
  file.open(filename, std::ios::in | std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    Exit("failed to open file");
  }
  std::streampos input_size = file.tellg();
  *size = input_size;
  if (input_size == -1) {
    Exit("file.tellg() failed");
  }
  VLOGF("file size is: %lld\n", static_cast<long long>(input_size));
  std::unique_ptr<uint8_t[]> raw_adts = std::make_unique<uint8_t[]>(input_size);
  file.seekg(0, std::ios::beg);
  if (!file) {
    Exit("file.seekg(0, beg) failed");
  }
  file.read(reinterpret_cast<char*>(raw_adts.get()), input_size);
  if (!file) {
    Exit("file.read() failed");
  }
  file.close();
  if (!file) {
    Exit("file.close() failed");
  }
  return raw_adts;
}

void PostSerial(async_t* async, fit::closure to_run) {
  zx_status_t post_result = async::PostTask(async, std::move(to_run));
  if (post_result != ZX_OK) {
    Exit("async::PostTask() failed - post_result: %d", post_result);
  }
}

void SHA256_Update_AudioParameters(SHA256_CTX* sha256_ctx,
                                   const fuchsia::mediacodec::PcmFormat& pcm) {
  uint32_t pcm_mode_le = htole32(pcm.pcm_mode);
  if (!SHA256_Update(sha256_ctx, &pcm_mode_le, sizeof(pcm_mode_le))) {
    assert(false);
  }
  uint32_t bits_per_sample_le = htole32(pcm.bits_per_sample);
  if (!SHA256_Update(sha256_ctx, &bits_per_sample_le,
                     sizeof(bits_per_sample_le))) {
    assert(false);
  }
  uint32_t frames_per_second_le = htole32(pcm.frames_per_second);
  if (!SHA256_Update(sha256_ctx, &frames_per_second_le,
                     sizeof(frames_per_second_le))) {
    assert(false);
  }
  for (fuchsia::mediacodec::AudioChannelId channel_id : *pcm.channel_map) {
    uint32_t channel_id_le = htole32(channel_id);
    if (!SHA256_Update(sha256_ctx, &channel_id_le, sizeof(channel_id_le))) {
      assert(false);
    }
  }
}
