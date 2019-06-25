// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_H_
#define TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_H_

#include <cstdint>
#include <ostream>

namespace fidlcat {

void ErrorName(int64_t error_code, std::ostream& os);

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_H_
