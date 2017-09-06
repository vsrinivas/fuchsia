// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/model.h"

namespace escher {

Model::Model() = default;

Model::~Model() = default;

Model::Model(std::vector<Object> objects) : objects_(std::move(objects)) {}

Model::Model(Model&& other) : objects_(std::move(other.objects_)) {
  other.objects_.clear();
  other.time_ = 0.f;
}

Model& Model::operator=(Model&& other) {
  objects_ = std::move(other.objects_);
  other.objects_.clear();
  time_ = other.time_;
  other.time_ = 0.f;

  return *this;
}

}  // namespace escher
