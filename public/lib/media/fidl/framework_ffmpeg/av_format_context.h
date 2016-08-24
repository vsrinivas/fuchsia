// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_FFMPEG_AV_FORMAT_CONTEXT_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_FFMPEG_AV_FORMAT_CONTEXT_H_

#include "apps/media/services/framework_ffmpeg/av_io_context.h"
#include "apps/media/services/framework_ffmpeg/ffmpeg_init.h"
extern "C" {
#include "third_party/ffmpeg/libavcodec/avcodec.h"
#include "third_party/ffmpeg/libavformat/avformat.h"
}

namespace mojo {
namespace media {

struct AVFormatContextDeleter {
  inline void operator()(AVFormatContext* ptr) const {
    avformat_free_context(ptr);
  }
};

using AvFormatContextPtr =
    std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;

struct AvFormatContext {
  static AvFormatContextPtr OpenInput(const AvIoContextPtr& io_context) {
    InitFfmpeg();

    AVFormatContext* format_context = avformat_alloc_context();
    format_context->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_FAST_SEEK;
    format_context->pb = io_context.get();

    // TODO(dalesat): This synchronous operation may take a long time.
    int r = avformat_open_input(&format_context, nullptr, nullptr, nullptr);
    if (r < 0) {
      delete format_context;
      format_context = nullptr;
    }

    return AvFormatContextPtr(format_context);
  }
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_FFMPEG_AV_FORMAT_CONTEXT_H_
