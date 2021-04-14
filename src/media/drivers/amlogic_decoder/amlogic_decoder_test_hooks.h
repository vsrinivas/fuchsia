// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_AMLOGIC_DECODER_TEST_HOOKS_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_AMLOGIC_DECODER_TEST_HOOKS_H_

namespace amlogic_decoder {

struct AmlogicDecoderTestHooks {
  bool force_context_save_restore = false;
};

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_AMLOGIC_DECODER_TEST_HOOKS_H_
