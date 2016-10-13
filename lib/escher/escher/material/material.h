// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace escher {

// TODO: be hashable/comparable.
class MaterialSpec {};

class Material {
 public:
  virtual const MaterialSpec& GetSpec() const = 0;
};

}  // namespace escher
