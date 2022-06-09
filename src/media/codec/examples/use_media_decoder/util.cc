// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <lib/async/dispatcher.h>
#include <lib/media/test/one_shot_event.h>
#include <stdarg.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <fstream>
#include <iostream>
#include <memory>

#include <fbl/auto_lock.h>

// Branchless implementation of pdep
inline void ProcessCoordinate(uint32_t tile_local_coordinate, uint32_t& mask_bit_idx,
                              uint32_t axis_mask, uint32_t axis_mask_bit_idx,
                              uint32_t& swizzled_offset) {
  // This has up to one bit set, depending on whether corresponding bit position of axis_mask
  // has that bit set. If 1 bit set, deposit currently-corresponding bit of mask in swizzled offset.
  // If no bits set, save currently-corresponding bit of mask for next iteration.
  uint32_t axis_bit = (1u << axis_mask_bit_idx) & axis_mask;

  // Deposit a bit of (original) mask into swizzled offset, shifted cumulatively to the left by how
  // many zeroes there have been in axis_mask so far (see pdep link for a diagram). Since we are
  // guaranteed that there is no overlap between the different axis_masks we can directly add the
  // output to the swizzle_offset.
  uint32_t deposit_bit = axis_bit & (tile_local_coordinate << mask_bit_idx);
  swizzled_offset += deposit_bit;

  // If axis_bit has a bit set, we used up a bit position of mask and can leave that bit position
  // behind by not incrementing the value of mask_bit_idx. If axis_bit has no bits set, we're
  // bringing the mask_bit_idx bit of mask along for the next iteration (along with higher-order
  // bits of mask), and by incrementing the value of mask_bit_idx, that bit will end up landing in
  // the correct position within swizzled_offset. The below code is a branchless equivalent to the
  // following:
  //
  // if (!axis_bit) mask_bit_idx += 1u;
  mask_bit_idx += ~(axis_bit >> axis_mask_bit_idx) & 0x1u;
}

uint32_t ConvertLinearToLegacyYTiled(uint32_t y_offset, uint32_t x_offset, uint32_t pitch) {
  static constexpr uint32_t kXMask = 0x0E0Fu;
  static constexpr uint32_t kXBits = 7u;
  static constexpr uint32_t kYMask = 0x01F0u;
  static constexpr uint32_t kYBits = 5u;
  static constexpr uint32_t kTotalBits = kXBits + kYBits;

  // Ensure the masks are not malformed
  static_assert((kXMask + kYMask) == (kXMask | kYMask), "X and/or Y mask is malformed");
  static_assert((kXMask | kYMask) < (1 << 16), "Mask can only contain 16 bits");

  // Ensure pitch is a multiple of 2^kXBits
  ZX_ASSERT(pitch == ((pitch >> kXBits) << kXBits));

  // Figure out the amount of tiles per row
  uint32_t tiles_per_row = (pitch >> kXBits);

  // First calculate the row and col (in terms of tiles) of where the address is
  uint32_t row = (y_offset >> kYBits);
  uint32_t col = (x_offset >> kXBits);

  // Next calculate the tile-local coordinates
  uint32_t y_coordinate = (y_offset & ((1 << kYBits) - 1));
  uint32_t x_coordinate = (x_offset & ((1 << kXBits) - 1));

  // First calculate the surface offset give the tile numbers
  uint32_t swizzled_offset = ((row * tiles_per_row) + col) << kTotalBits;

  // Would be nice if _pdep_u32 was supported but currently Fuchsia only supports x86-64. The
  // BMI2 extension, which _pdep_u32 is a part of, is supported in x86-64-v3. Currently requiring
  // processors to support BMI2 should exclude too many older generation architectures as outlined
  // in https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0073_x86_64_platform_requirement.
  // If Fuchsia ever upgrades to x86-64-v3 the below code can be upgraded to use the _pdep_u32
  // intrinsic like so:
  //
  // swizzled_offset += _pdep_u32(x, kXMask);
  // swizzled_offset += _pdep_u32(y, kYMask);
  //
  // For a more detailed explanation of pedp please read the following:
  // https://www.felixcloutier.com/x86/pdep

  uint32_t axis_bit_idx = 0u, x_bit_idx = 0u, y_bit_idx = 0u;

  while (axis_bit_idx < kTotalBits) {
    // Process the x and y coordinates; due to no bits shared between kXMask and kYMask, and
    // the fact that every bit mask has a corresponding 1 in exactly one of kXMask or kYMask but not
    // both, we know that we'll be depositing a bit from x or from y into swizzled_offset, but not
    // from both.  Also, we know we'll be consuming a bit from (not shifting) exactly one of x or y
    // (whichever deposited a bit into swizzled_offset).
    ProcessCoordinate(x_coordinate, x_bit_idx, kXMask, axis_bit_idx, swizzled_offset);
    ProcessCoordinate(y_coordinate, y_bit_idx, kYMask, axis_bit_idx, swizzled_offset);

    // Increment the axis_bit_idx by one. Loop again to process the next bit index with the
    // corresponding bit mask
    axis_bit_idx += 1u;
  }

  return swizzled_offset;
}

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
  size_t buffer_bytes_2 = vsnprintf(buffer.get(), buffer_bytes, format, args) + 1;
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
    Exit("failed to open file %s", filename);
  }
  std::streampos input_size = file.tellg();
  *size = input_size;
  if (input_size == -1) {
    Exit("file.tellg() failed");
  }
  VLOGF("file size is: %lld", static_cast<long long>(input_size));
  auto raw_adts = std::make_unique<uint8_t[]>(input_size);
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

void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run) {
  zx_status_t post_result = async::PostTask(dispatcher, std::move(to_run));
  if (post_result != ZX_OK) {
    Exit("async::PostTask() failed - post_result: %d", post_result);
  }
}

// Fence through any lambdas previously posted with PostSerial().
void FencePostSerial(async_dispatcher_t* dispatcher) {
  // If this gets stuck, make sure this isn't being called on the dispatcher's
  // own thread, and consider asserting in the caller that this isn't getting
  // called on the dispatcher's thread.
  OneShotEvent one_shot;
  PostSerial(dispatcher, [&one_shot] { one_shot.Signal(); });
  // Wait indefinitely.
  one_shot.Wait();
}

void SHA256_Update_AudioParameters(SHA256_CTX* sha256_ctx, const fuchsia::media::PcmFormat& pcm) {
  uint32_t pcm_mode_le = htole32(pcm.pcm_mode);
  if (!SHA256_Update(sha256_ctx, &pcm_mode_le, sizeof(pcm_mode_le))) {
    assert(false);
  }
  uint32_t bits_per_sample_le = htole32(pcm.bits_per_sample);
  if (!SHA256_Update(sha256_ctx, &bits_per_sample_le, sizeof(bits_per_sample_le))) {
    assert(false);
  }
  uint32_t frames_per_second_le = htole32(pcm.frames_per_second);
  if (!SHA256_Update(sha256_ctx, &frames_per_second_le, sizeof(frames_per_second_le))) {
    assert(false);
  }
  for (fuchsia::media::AudioChannelId channel_id : pcm.channel_map) {
    uint32_t channel_id_le = htole32(channel_id);
    if (!SHA256_Update(sha256_ctx, &channel_id_le, sizeof(channel_id_le))) {
      assert(false);
    }
  }
}

void SHA256_Update_VideoParameters(SHA256_CTX* sha256_ctx,
                                   const fuchsia::media::VideoUncompressedFormat& video) {
  UpdateSha256(sha256_ctx, video.fourcc);
  UpdateSha256(sha256_ctx, video.primary_width_pixels);
  UpdateSha256(sha256_ctx, video.primary_height_pixels);
  UpdateSha256(sha256_ctx, video.secondary_width_pixels);
  UpdateSha256(sha256_ctx, video.secondary_height_pixels);
  UpdateSha256(sha256_ctx, video.planar);
  UpdateSha256(sha256_ctx, video.swizzled);
  UpdateSha256(sha256_ctx, video.primary_line_stride_bytes);
  UpdateSha256(sha256_ctx, video.secondary_line_stride_bytes);
  UpdateSha256(sha256_ctx, video.primary_start_offset);
  UpdateSha256(sha256_ctx, video.secondary_start_offset);
  UpdateSha256(sha256_ctx, video.tertiary_start_offset);
  UpdateSha256(sha256_ctx, video.primary_pixel_stride);
  UpdateSha256(sha256_ctx, video.secondary_pixel_stride);
}

void SHA256_Update_VideoPlane(SHA256_CTX* sha256_ctx, uint8_t* start, uint32_t width,
                              uint32_t stride, uint32_t height) {
  uint8_t* src = start;
  for (uint32_t row = 0; row < height; ++row) {
    SHA256_Update(sha256_ctx, src, width);
    src += stride;
  }
}
