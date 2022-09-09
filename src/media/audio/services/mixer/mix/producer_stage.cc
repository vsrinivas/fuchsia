// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/producer_stage.h"

namespace media_audio {

void ProducerStage::UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) {
  set_presentation_time_to_frac_frame(f);
}

}  // namespace media_audio
