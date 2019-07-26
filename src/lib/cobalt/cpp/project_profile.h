// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl.h>

#include "lib/fsl/vmo/sized_vmo.h"

namespace cobalt {

fuchsia::cobalt::ProjectProfile ProjectProfileFromBase64String(const std::string &cfg);

fuchsia::cobalt::ProjectProfile ProjectProfileFromString(const std::string &cfg);

fuchsia::cobalt::ProjectProfile ProjectProfileFromFile(const std::string &filename);

fuchsia::cobalt::ProjectProfile ProjectProfileFromVmo(fsl::SizedVmo vmo);

}  // namespace cobalt
