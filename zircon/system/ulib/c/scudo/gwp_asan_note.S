// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gwp_asan_info.h"

.pushsection .note.gwp-asan-info, "aR", %note
  .balign 4
  .int .Lname_end - .Lname  // n_namesz
  .int .Ldesc_end - .Ldesc  // n_descsz
  .int GWP_ASAN_NOTE_TYPE   // n_type
.Lname:
  .string "GWP-ASan"
.Lname_end:
  .balign 4
.Ldesc:
  // __libc_gwp_asan_info is defined in gwp_asan_info.cc.
  .quad __libc_gwp_asan_info - .Ldesc
.Ldesc_end:
  .balign 4
.popsection
