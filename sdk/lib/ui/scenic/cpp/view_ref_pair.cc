// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/assert.h>

namespace scenic {

ViewRefPair ViewRefPair::New() {
  ViewRefPair ref_pair;

  auto status = zx::eventpair::create(/*options*/ 0u, &ref_pair.control_ref.reference,
                                      &ref_pair.view_ref.reference);
  // Assert even in non-debug builds, because eventpair creation can fail under
  // normal operation.  Failure can occur for example, if the job creation
  // policy governing this process forbids eventpair creation.
  //
  // It is unlikely that a well-behaved Scenic client would crash here; if you
  // hit this, it means something is very abnormal.
  ZX_ASSERT(status == ZX_OK);

  // Remove duplication from control_ref; Scenic requires it.
  ref_pair.control_ref.reference.replace(ZX_DEFAULT_EVENTPAIR_RIGHTS & (~ZX_RIGHT_DUPLICATE),
                                         &ref_pair.control_ref.reference);

  // Remove signaling from view_ref; Scenic requires it.
  ref_pair.view_ref.reference.replace(ZX_RIGHTS_BASIC, &ref_pair.view_ref.reference);
  return ref_pair;
}

}  // namespace scenic
