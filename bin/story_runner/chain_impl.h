// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_STORY_RUNNER_CHAIN_IMPL_H_
#define PERIDOT_BIN_STORY_RUNNER_CHAIN_IMPL_H_

#include <vector>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/module/fidl/module_data.fidl.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/story/fidl/story_controller.fidl.h"

namespace modular {

class ChainImpl {
 public:
  ChainImpl(f1dl::VectorPtr<f1dl::StringPtr> path, ChainDataPtr chain_data);
  ~ChainImpl();

  const f1dl::VectorPtr<f1dl::StringPtr>& chain_path() const { return path_; }

  LinkPathPtr GetLinkPathForKey(const f1dl::StringPtr& key);

 private:
  const f1dl::VectorPtr<f1dl::StringPtr> path_;
  const ChainDataPtr chain_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChainImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_CHAIN_IMPL_H_
