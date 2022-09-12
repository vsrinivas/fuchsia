// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_FACTORY_CODEC_ISOLATE_H_
#define SRC_MEDIA_CODEC_FACTORY_CODEC_ISOLATE_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

enum class IsolateType {
  kSw,
  kMagma,
};

void ForwardToIsolate(std::string component_url, bool is_v2, IsolateType type,
                      sys::ComponentContext* component_context,
                      fit::function<void(fuchsia::mediacodec::CodecFactoryPtr)> connect_func,
                      fit::function<void(void)> failure_func);

#endif  // SRC_MEDIA_CODEC_FACTORY_CODEC_ISOLATE_H_
