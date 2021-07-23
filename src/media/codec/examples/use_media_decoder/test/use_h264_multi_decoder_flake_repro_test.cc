// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This manual test is a basic integration test of the codec_factory +
// amlogic_video_decoder driver.
//
// If this test breaks and it's not immediately obvoius why, please feel free to
// involve dustingreen@ (me) in figuring it out.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/media/codec_impl/fourcc.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <set>

#include "../use_video_decoder.h"
#include "../util.h"
#include "use_video_decoder_test.h"

// This test can be run manually along with modifications to the decoder to help narrow down any
// glitches.

namespace {

constexpr char kInputFilePath[] = "/pkg/data/bear.h264";
constexpr int kInputFileFrameCount = 16;

const char* kGoldenSha256 = "a4418265eaa493604731d6871523ac2a0d606f40cddd48e2a8cd0b0aa5f152e1";

// Must be nullptr terminated.
const char* kPerFrameGoldenSha256[] = {
    "60fcf7052bfaf2008f90d0acd8d241d40494fe866e2f4d743c8d4bd350037f5c",
    "cfb69a06f1e40c27ce7efe496fabaaaa5aac33e7d75aebbc171b774f39dec0e7",
    "cebdc567cde3994f7af6a1834e5bd2125436171fdfd596e2fbe660548f907bf2",
    "b4add9c28d83ab3bb161c99137607942600bb5db5557f4831a90536d5316c3f1",
    "08a536401727c657e9d8128ed4da734b1a8cc84ea0bf612d17eecd0bc28b0980",
    "1bd326a527d6d66019ef728e2392e1d9435b66db558a65dc962cc0739595e2b2",
    "5288f119f0f0d40acce3cd2a8b8ec131a9261dc5f044a7a68f1b8b797329c031",
    "e8b35ef93fa68392447840b381874c985041a4b7909be33fe28bdebbe470b255",
    "55cb3a2e795b3bba5b6dd5c2f34d7dbd0620ca5c9aa049a82b0f83163b01fe8d",
    "d89253d73f0cfbe952a154796b523f87fcb2a168f811ec7eae7a48bbebb3413d",
    "3d058abededa8fd8805e691478f882e870c8f622c8cd955ca69c0cba0e76eee6",
    "03e102f807110d01e0be67c4296d9296bd633f8a88141c5419d7d4a5d027c1e4",
    "2be24f256fd1e073b65c11a674b06dd69083a44e025e9bd4b25414fbfac5def2",
    "57fd5bcb50893141e89132729ebf1f034e500082c992edf2d939c3237a7d9641",
    "041ca5c2323de87c935515963b7ab635f866d4e9f7b7dbd20f053a21c0114133",
    "e8aee59b8c84a65e45dd51042be10136ec44fd55d70ccb3f49a25da25bae4b02",
    "6d4462cc530a4dc9068382cd5911cbdef17ffd1bf2fe74b33d1963a0c94996ec",
    "e338d68d078bbec4b6c5a88a440f5fcd2d32dc00a60fc34f4bed9e3320be70fe",
    "60d1fdade21ee0e5abbc3e8bc96e67dfc0482c7b44f9cef104930a8344b20942",
    "1db6e4b8ddb8d85e6946783633cbcb0678bf3aee77576b32d5afb26d6aef64ef",
    "a22bca17a863be3bd348dbe9937b39aebafdde81b97891d3311b8c21d428df39",
    "3d9e876949e87df6cf83e2352ecfe85eabc08b70c8b135e54678ee7375a3fada",
    "80dcb60f02a6563da9d40626ed8f68ba428bf3790e65b2da60a2a7d4712f41c8",
    "85b6934b4ebe8fece109b4d43f50b0a0a7ba433cb44151dd057cfd7bfd053b39",
    "b9a12adc8334de0aa40588effe287ef3b42179a3f0c3d338cf127c8f5d99ffaa",
    "ea9030ec27efea65c9413a721e2a502c83abf5975777e3da127e7a34911c7ae4",
    "6ede11d2f21d6df42683742dd24ac61e53c430aec6518942833cb4e7867ad85a",
    "04d7152356f7c793c8e12218d0c1976facc0972f7ec1d63397cdbf860f95df3f",
    "da59d28a16cb4f9d29688809341f2797edf0462a1c954a3b2f77750e7edb6a90",
    "a4418265eaa493604731d6871523ac2a0d606f40cddd48e2a8cd0b0aa5f152e1",
    nullptr};

}  // namespace

int main(int argc, char* argv[]) {
  UseVideoDecoderTestParams test_params = {
      .loop_stream_count = 1000000000,
      .reset_hash_each_iteration = true,
      .mime_type = "video/h264",
      .per_frame_golden_sha256 = kPerFrameGoldenSha256,
      .golden_sha256 = kGoldenSha256,
  };
  return use_video_decoder_test(kInputFilePath, kInputFileFrameCount, use_h264_decoder,
                                /*is_secure_output=*/false, /*is_secure_input=*/false,
                                /*min_output_buffer_count=*/0, &test_params);
}
