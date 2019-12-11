// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_USE_VIDEO_DECODER_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_USE_VIDEO_DECODER_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <stdint.h>

#include <openssl/sha.h>

class FrameSink;
class InStreamPeeker;
class InputCopier;

// An EmitFrame is passed I420 frames with stride == width, and with width
// and height being display_width and display_height (not coded_width and
// coded_height).  The width and height must be even.
typedef fit::function<void(uint8_t* i420_data, uint32_t width, uint32_t height, uint32_t stride,
                           bool has_timestamp_ish, uint64_t timestamp_ish)>
    EmitFrame;

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
// fidl_loop - the loop created and run/started by main().  The codec_factory is
//     and sysmem are bound to fidl_loop->dispatcher().
// fidl_thread - the thread on which fidl_loop activity runs.
// codec_factory - codec_factory to take ownership of, use, and close by the
//     time the function returns.
// fourcc_out - if not nullptr, sets the value to the fourcc of the decoded
//     frames.
// frame_sink - if not nullptr, send each frame to this FrameSink, which will
//     call back when the frame has been released by the sink.
// emit_frame - if set, is called to emit each frame in i420 format + timestamp
//     info.
void use_h264_decoder(async::Loop* fidl_loop, thrd_t fidl_thread,
                      fuchsia::mediacodec::CodecFactoryPtr codec_factory,
                      fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem,
                      InStreamPeeker* in_stream, InputCopier* input_copier,
                      uint64_t min_output_buffer_size, uint32_t min_output_buffer_count,
                      bool is_secure_output, bool is_secure_input, FrameSink* frame_sink,
                      EmitFrame emit_frame);

// The same as use_h264_decoder, but for a VP9 file wrapped in an IVF container.
void use_vp9_decoder(async::Loop* fidl_loop, thrd_t fidl_thread,
                     fuchsia::mediacodec::CodecFactoryPtr codec_factory,
                     fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem,
                     InStreamPeeker* in_stream, InputCopier* input_copier,
                     uint64_t min_output_buffer_size, uint32_t min_output_buffer_count,
                     bool is_secure_output, bool is_secure_input, FrameSink* frame_sink,
                     EmitFrame emit_frame);

// Common function pointer type shared by use_h264_decoder, use_vp9_decoder.
typedef void (*UseVideoDecoderFunction)(
    async::Loop* fidl_loop, thrd_t fidl_thread, fuchsia::mediacodec::CodecFactoryPtr codec_factory,
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem, InStreamPeeker* in_stream,
    InputCopier* input_copier, uint64_t min_output_buffer_size, uint32_t min_output_buffer_count,
    bool is_secure_output, bool is_secure_input, FrameSink* frame_sink, EmitFrame emit_frame);

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_USE_VIDEO_DECODER_H_
