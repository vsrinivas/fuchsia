// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fitx/result.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/memory.h>
#include <mexec.h>

#include <fbl/array.h>
#include <ktl/byte.h>
#include <phys/handoff.h>

fitx::result<fitx::failed> ArchAppendMexecDataFromHandoff(MexecDataImage& image,
                                                          PhysHandoff& handoff) {
  return fitx::ok();
}
