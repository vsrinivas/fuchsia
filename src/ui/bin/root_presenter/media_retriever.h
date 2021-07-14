// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_MEDIA_RETRIEVER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_MEDIA_RETRIEVER_H_

#include <fuchsia/io/cpp/fidl.h>

namespace root_presenter {

class MediaRetriever {
 public:
  using ResetSoundResult = fpromise::result<fidl::InterfaceHandle<fuchsia::io::File>, zx_status_t>;

  virtual ~MediaRetriever();
  virtual ResetSoundResult GetResetSound();
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_MEDIA_RETRIEVER_H_
