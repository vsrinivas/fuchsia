// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/view_creation_tokens.h>

using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;

namespace scenic {
ViewCreationTokenPair ViewCreationTokenPair::New() {
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  zx::channel::create(0, &parent_token.value, &child_token.value);
  return {.view_token = std::move(child_token), .viewport_token = std::move(parent_token)};
}

}  // namespace scenic
