// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_EXAMPLES_NOODLES_RASTERIZER_H_
#define APPS_MOZART_EXAMPLES_NOODLES_RASTERIZER_H_

#include <memory>

#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "apps/mozart/services/composition/scenes.fidl.h"
#include "lib/ftl/macros.h"

namespace examples {

class Frame;

// Skia-based rasterizer which runs on a separate thread from the view.
// Calls into this object, including its creation, must be posted to the
// correct message loop by the view.
class Rasterizer {
 public:
  Rasterizer(mozart::ScenePtr scene);

  ~Rasterizer();

  void PublishFrame(std::unique_ptr<Frame> frame);

 private:
  mozart::ScenePtr scene_;
  mozart::BufferProducer buffer_producer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Rasterizer);
};

}  // namespace examples

#endif  // APPS_MOZART_EXAMPLES_NOODLES_RASTERIZER_H_
