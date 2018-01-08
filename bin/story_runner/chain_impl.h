// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_STORY_RUNNER_CHAIN_IMPL_H_
#define PERIDOT_BIN_STORY_RUNNER_CHAIN_IMPL_H_

#include <vector>

#include "lib/story/fidl/chain.fidl.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/fxl/macros.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace modular {

class ChainImpl : Chain {
 public:
  ChainImpl();
  ~ChainImpl() override;

  void Connect(fidl::InterfaceRequest<Chain> request);

 private:
  void GetKeys(const GetKeysCallback& done) override;
  void GetLink(const fidl::String& key, fidl::InterfaceRequest<Link> request) override;

  fidl::BindingSet<Chain> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChainImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_CHAIN_IMPL_H_
