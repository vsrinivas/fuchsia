// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

struct FormatValueOptions;
class Frame;
class OutputBuffer;
class Thread;
class ValueFormatHelper;

// Outputs the list of frames to the console. This will complete asynchronously
// if the frames are not currently available.
void OutputFrameList(Thread* thread, bool long_format);

// Formats one frame using the short format to the output buffer. The frame ID
// will be printed if supplied. If the ID is -1, it will be omitted.
//
// This does not append a newline at the end of the output.
void FormatFrame(const Frame* frame, OutputBuffer* out, int id = -1);

// Formats one frame using the long format. Since the long format includes
// function parameters which are computed asynchronously, this takes the
// asynchronous ValueFormatHelper as the output.
//
// This does not append a newline at the end of the output.
void FormatFrameLong(const Frame* frame, ValueFormatHelper* out,
                     const FormatValueOptions& options, int id = -1);

}  // namespace zxdb
