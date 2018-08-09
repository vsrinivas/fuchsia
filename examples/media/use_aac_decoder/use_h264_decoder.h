// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_USE_H264_DECODER_H_
#define GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_USE_H264_DECODER_H_

#include <openssl/sha.h>
#include <stdint.h>

#include <fuchsia/mediacodec/cpp/fidl.h>

// use_h264_decoder()
//
// If anything goes wrong, exit(-1) is used directly (until we have any reason
// to do otherwise).
//
// On success, the return value is the sha256 of the output data. This is
// intended as a golden-file value when this function is used as part of a test.
// This sha256 value accounts for all the output payload data and also the
// output format parameters. When the same input file is decoded we expect the
// sha256 to be the same.
//
// codec_factory - codec_factory to take ownership of, use, and close by the
//     time the function returns.
// input_file - This must be set and must be the filename of an input h264
//     file (input file extension not checked / doesn't matter).
// output_file - If nullptr, don't write the data to an output file.  If
//     non-nullptr, output uncompressed data to the specified file.  When used
//     as an example, this will tend to be set.  When used as a test, this will
//     not be set.
// md_out - out sha256 of the ordered output frame pixels and ordered output
//     format details.
// timestamps_out - out ordered <has_timestamp_ish, timestamp_ish> seen at the
//     output of the decoder.
void use_h264_decoder(fuchsia::mediacodec::CodecFactoryPtr codec_factory,
                      const std::string& input_file,
                      const std::string& output_file,
                      uint8_t md_out[SHA256_DIGEST_LENGTH],
                      std::vector<std::pair<bool, uint64_t>>* timestamps_out);

#endif  // GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_USE_H264_DECODER_H_
