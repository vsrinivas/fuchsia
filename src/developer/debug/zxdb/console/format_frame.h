// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/console/console.h"

namespace zxdb {

class FormatValue;
struct FormatExprValueOptions;
class Frame;
class OutputBuffer;
class Thread;

// Outputs the list of frames to the console. This will complete asynchronously
// if the frames are not currently available. Printing of function parameter
// types is controlled by include_params.
void OutputFrameList(Thread* thread, bool include_params, bool long_format);

// Formats one frame using the short format to the output buffer. The frame ID
// will be printed if supplied. If the ID is -1, it will be omitted.
//
// Printing of function parameter types is controlled by include_params.
//
// This does not append a newline at the end of the output.
void FormatFrame(const Frame* frame, bool include_params, OutputBuffer* out,
                 int id = -1);

// Formats one frame using the long format. Since the long format includes
// function parameters which are computed asynchronously, this takes the
// asynchronous FormatValue as the output.
//
// This does not append a newline at the end of the output.
void FormatFrameLong(const Frame* frame, bool include_params, FormatValue* out,
                     const FormatExprValueOptions& options, int id = -1);

// Asynchronously outputs a description of the current frame.
void FormatFrameAsync(ConsoleContext* context, Target* target, Thread* thread,
                      Frame* frame, bool force_types = false);

}  // namespace zxdb
