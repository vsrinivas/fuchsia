// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_MATCHERS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_MATCHERS_H_

namespace media::audio::testing {

MATCHER(VolumeMappingEq, "Equality matcher for VolumeMapping") {
  return static_cast<::testing::Matcher<float>>(::testing::FloatEq(std::get<0>(arg).volume))
             .MatchAndExplain(std::get<1>(arg).volume, result_listener) &&
         static_cast<::testing::Matcher<float>>(::testing::FloatEq(std::get<0>(arg).gain_dbfs))
             .MatchAndExplain(std::get<1>(arg).gain_dbfs, result_listener);
}

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_MATCHERS_H_
