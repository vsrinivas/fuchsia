// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/self.h>

namespace elfldltl {

// All the self.h calls are normally inlined.  But in unoptimized code,
// they won't get defined with vague linkage because of the extern
// template declarations.  So these explicit instantiations here
// generate the external-linkage entry points for the library.

template class Self<ElfClass::k32>;
template class Self<ElfClass::k64>;

}  // namespace elfldltl
