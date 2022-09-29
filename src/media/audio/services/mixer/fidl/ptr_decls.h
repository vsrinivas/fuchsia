// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_PTR_DECLS_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_PTR_DECLS_H_

#include <memory>

namespace media_audio {

// This file exists to break circular dependencies.
// Since shared_ptr use is ubiquitous, we use XPtr as a more concise name for std::shared_ptr<X>.

class Node;
using NodePtr = std::shared_ptr<Node>;

class GraphDetachedThread;
using GraphDetachedThreadPtr = std::shared_ptr<GraphDetachedThread>;

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_PTR_DECLS_H_
