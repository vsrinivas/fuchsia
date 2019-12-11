// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_UTIL_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_UTIL_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>

#include <openssl/sha.h>

#include "src/lib/fxl/logging.h"

#define VLOG_ENABLED 0

#if (VLOG_ENABLED)
#define VLOGF(fmt, ...)              \
  do {                               \
    printf(fmt "\n", ##__VA_ARGS__); \
    fflush(stdout);                  \
  } while (0)
#else
#define VLOGF(fmt, ...) \
  do {                  \
  } while (0)
#endif

#define LOGF(fmt, ...)               \
  do {                               \
    printf(fmt "\n", ##__VA_ARGS__); \
    fflush(stdout);                  \
  } while (0)

void Exit(const char* format, ...);

std::unique_ptr<uint8_t[]> read_whole_file(const char* filename, size_t* size);

// Post to dispatcher in a way that's guaranteed to run the posted work in the
// same order as the posting order (is the intent - if async::PostTask ever
// changes to not guarantee order, we'll need to work around that here).
//
// TODO(dustingreen): Determine if async::PostTask() intends to strictly
// guarantee order.
void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);

// Fence through any lambdas previously posted with PostSerial().
void FencePostSerial(async_dispatcher_t* dispatcher);

template <typename T>
void UpdateSha256(SHA256_CTX* ctx, T field) {
  T field_le;
  switch (sizeof(field)) {
    case 8:
      field_le = htole64(field);
      break;
    case 4:
      field_le = htole32(field);
      break;
    case 2:
      field_le = htole16(field);
      break;
    case 1:
      field_le = field;
      break;
    default:
      Exit("UpdateSha256 unexpected field size");
  }
  if (!SHA256_Update(ctx, &field_le, sizeof(field_le))) {
    FXL_CHECK(false) << "SHA256_Update() failed in UpdateSha256()";
  }
}

void SHA256_Update_AudioParameters(SHA256_CTX* sha256_ctx, const fuchsia::media::PcmFormat& pcm);

void SHA256_Update_VideoParameters(SHA256_CTX* sha256_ctx,
                                   const fuchsia::media::VideoUncompressedFormat& video);

void SHA256_Update_VideoPlane(SHA256_CTX* sha256_ctx, uint8_t* start, uint32_t width,
                              uint32_t stride, uint32_t height);

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_UTIL_H_
