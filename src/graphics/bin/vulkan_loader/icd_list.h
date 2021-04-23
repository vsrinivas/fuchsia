// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_BIN_VULKAN_LOADER_ICD_LIST_H_
#define SRC_GRAPHICS_BIN_VULKAN_LOADER_ICD_LIST_H_

#include <memory>
#include <vector>

#include "src/graphics/bin/vulkan_loader/icd_component.h"

// This class holds an ordered list of components, so that VMOs can be looked up from them in
// priority order.
class IcdList {
 public:
  IcdList();

  void Add(std::shared_ptr<IcdComponent> component) { components_.push_back(std::move(component)); }

  // Finds an ICD in the list with a `library_path` matching this string.
  std::optional<zx::vmo> GetVmoMatchingSystemLib(const std::string& library_path) const;

 private:
  std::vector<std::shared_ptr<IcdComponent>> components_;
};

#endif  // SRC_GRAPHICS_BIN_VULKAN_LOADER_ICD_LIST_H_
