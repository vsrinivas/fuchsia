// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/helpers.h"

#include "src/lib/fxl/logging.h"

namespace utils {

zx::event CopyEvent(const zx::event& event) {
  zx::event event_copy;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy) != ZX_OK)
    FXL_LOG(ERROR) << "Copying zx::event failed.";
  return event_copy;
}

fuchsia::ui::scenic::Present2Args CreatePresent2Args(zx_time_t requested_presentation_time,
                                                     std::vector<zx::event> acquire_fences,
                                                     std::vector<zx::event> release_fences,
                                                     zx_duration_t requested_prediction_span) {
  fuchsia::ui::scenic::Present2Args args;
  args.set_requested_presentation_time(requested_presentation_time);
  args.set_acquire_fences(std::move(acquire_fences));
  args.set_release_fences(std::move(release_fences));
  args.set_requested_prediction_span(requested_prediction_span);

  return args;
}

}  // namespace utils
