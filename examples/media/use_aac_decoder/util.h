// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_UTIL_H_
#define GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_UTIL_H_

#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <memory>

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include <openssl/sha.h>

#include <fuchsia/mediacodec/cpp/fidl.h>

#define VLOG_ENABLED 0

#if (VLOG_ENABLED)
#define VLOGF(...) printf(__VA_ARGS__)
#else
#define VLOGF(...) \
  do {             \
  } while (0)
#endif

#define LOGF(...) printf(__VA_ARGS__)

void Exit(const char* format, ...);

std::unique_ptr<uint8_t[]> read_whole_file(const char* filename, size_t* size);

// Post to async in a way that's guaranteed to run the posted work in the same
// order as the posting order (is the intent - if async::PostTask ever changes
// to not guarantee order, we'll need to work around that here).
//
// TODO(dustingreen): Determine if async::PostTask() intends to strictly
// guarantee order.
void PostSerial(async_t* async, fit::closure to_run);

void SHA256_Update_AudioParameters(SHA256_CTX* sha256_ctx,
                                   const fuchsia::mediacodec::PcmFormat& pcm);

#endif  // GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_UTIL_H_
