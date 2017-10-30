// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_AGENTS_IDEAS_H_
#define PERIDOT_BIN_AGENTS_IDEAS_H_

namespace maxwell {
namespace agents {

class IdeasAgent {
 public:
  virtual ~IdeasAgent() {}
  static constexpr char kIdeaId[] = "";
};

}  // namespace agents
}  // namespace maxwell

#endif  // PERIDOT_BIN_AGENTS_IDEAS_H_
