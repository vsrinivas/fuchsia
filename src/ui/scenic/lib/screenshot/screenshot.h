// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_H_
#define SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <unordered_set>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"

namespace screenshot {

class Screenshot : public fuchsia::ui::composition::Screenshot {
 public:
  Screenshot(fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> request,
             const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
                 buffer_collection_importers);

  void CreateImage(fuchsia::ui::composition::CreateImageArgs args,
                   CreateImageCallback callback) override;

 private:
  // Clients cannot use zero as an Image ID.
  static constexpr int64_t kInvalidId = 0;

  fidl::Binding<fuchsia::ui::composition::Screenshot> binding_;
  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers_;

  // Holds all registered images.
  std::unordered_set<int64_t> image_id_set_;
};

}  // namespace screenshot

#endif  // SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_H_
