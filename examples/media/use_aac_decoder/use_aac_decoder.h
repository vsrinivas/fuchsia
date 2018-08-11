// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_USE_AAC_DECODER_H_
#define GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_USE_AAC_DECODER_H_

#include <openssl/sha.h>
#include <stdint.h>

#include <fuchsia/mediacodec/cpp/fidl.h>

// use_aac_decoder()
//
// If anything goes wrong, exit(-1) is used directly (until we have any reason
// to do otherwise).
//
// On success, the return value is the crc32 of the output wav data.  This is
// intended as a golden-file value when this function is used as part of a test.
// This crc32 value accounts for all the output WAV data and also the audio
// output format parameters.  When the same input file is decoded we expect the
// crc32 to be the same.
//
// codec_factory - codec_factory to take ownership of, use, and close by the
//     time the function returns.
// input_file - This is the filename of an input .adts file (input file
//     extension not checked / doesn't matter).
// output_file - If empty, don't write the audio data to a wav file.  If
//     non-empty, output audio data to the specified wav file.  When used as
//     an example, this will tend to be set.  When used as a test, this will not
//     be set.
void use_aac_decoder(async_dispatcher_t* codec_factory_dispatcher,
                     fuchsia::mediacodec::CodecFactoryPtr codec_factory,
                     const std::string& input_adts_file,
                     const std::string& output_wav_file,
                     uint8_t out_md[SHA256_DIGEST_LENGTH]);

#endif  // GARNET_EXAMPLES_MEDIA_USE_AAC_DECODER_USE_AAC_DECODER_H_
