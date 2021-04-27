// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_THREAD_ROLE_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_THREAD_ROLE_H_

namespace amlogic_decoder {

// Loosely defined roles for the various threads of the amlogic decoder. Used to specify which
// profile to use on a thread.
// TODO(fxbug.dev/40858): When we switch to a role-based API, this enum can probably be removed in
// favor of role strings.
enum class ThreadRole {
  kSharedFidl,
  kParserIrq,
  kVdec0Irq,
  kVdec1Irq,
  kH264MultiCore,
  kH264MultiStreamControl,
  kVp9InputProcessing,
  kVp9StreamControl,
};

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_THREAD_ROLE_H_
