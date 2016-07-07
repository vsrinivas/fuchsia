// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "ftl/macros.h"
#include "escher/scene/object.h"

namespace escher {

// The model to render.
//
// TODO(jeffbrown): This currently only contains a vector of objects to be
// rendered but later on we may store additional resources used to
// interpret the contents of the model (such as tunable parameters).
class Model {
 public:
  Model();
  explicit Model(std::vector<Object> objects);
  ~Model();

  Model(Model&& other);
  Model& operator=(Model&& other);

  // Objects in back to front draw order.
  const std::vector<Object>& objects() const { return objects_; }

 private:
  std::vector<Object> objects_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Model);
};

}  // namespace escher
