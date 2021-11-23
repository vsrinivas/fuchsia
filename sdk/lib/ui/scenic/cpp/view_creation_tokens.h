// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_VIEW_CREATION_TOKENS_H_
#define LIB_UI_SCENIC_CPP_VIEW_CREATION_TOKENS_H_

#include <fuchsia/ui/input/cpp/fidl.h>

namespace scenic {

struct ViewCreationTokenPair {
  // Convenience function.
  static ViewCreationTokenPair New();

  fuchsia::ui::views::ViewCreationToken view_token;
  fuchsia::ui::views::ViewportCreationToken viewport_token;
};

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_VIEW_CREATION_TOKENS_H_
