// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace maxwell {
namespace agents {

class ModuleSuggesterAgent {
 public:
  virtual ~ModuleSuggesterAgent() {}
  static constexpr char kModuleSuggestionId[] = "module suggestion";
};

}  // namespace agents
}  // namespace maxwell
