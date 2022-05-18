// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_FFMPEG_AV_IO_CONTEXT_H_
#define SRC_MEDIA_VNEXT_LIB_FFMPEG_AV_IO_CONTEXT_H_

#include <lib/async/cpp/task.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

#include <condition_variable>
#include <mutex>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/vnext/lib/reader/reader.h"
extern "C" {
#include "libavformat/avio.h"
}

namespace fmlib {

struct AVIOContextDeleter {
  void operator()(AVIOContext* context) const;
};

using AvIoContextPtr = std::unique_ptr<AVIOContext, AVIOContextDeleter>;

class AvIoContext {
 public:
  // Creates an ffmpeg |AVIOContext| for a given reader. |reader| describes capabilities relating
  // to the content source and provides read access to that source. |dispatcher| identifies the
  // thread on which |reader| will be called, which must not be the same as the calling thread.
  //
  // The result, if successful, is the ffmpeg |AVIOContext| wrapped as a |AvIoContextPtr| for
  // memory safety. If the reader's |Describe| method fails, this method will fail, passing on the
  // status returned by the reader.
  //
  // The ffmpeg |AVIOContext| code makes blocking read and seek calls, so the reader that implements
  // (asynchronously) those read and seek calls must run on a different thread than the ffmpeg
  // |AVIOContext| code itself. This is typically accomplished by running the demux on its own
  // thread while the reader runs in the general FIDL thread or some other thread.
  //
  // This method blocks for the duration of a call to |reader->Describe|.
  static fpromise::result<AvIoContextPtr, zx_status_t> Create(std::shared_ptr<Reader> reader,
                                                              async_dispatcher_t* dispatcher);
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_FFMPEG_AV_IO_CONTEXT_H_
