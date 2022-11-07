// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FHCP_CPP_FHCP_H_
#define LIB_FHCP_CPP_FHCP_H_

#include <zircon/compiler.h>

namespace fhcp {

// Prints a message for display to a manual tester conducting an FHCP test.
//
// This message will only be printed on the host, not on the device under test.
//
// TODO(b/257287147): Provide a way to print a message on the device under test as well.
//
// A terminating newline is automatically appended to the end.
void PrintManualTestingMessage(const char* format, ...) __PRINTFLIKE(1, 2);

}  // namespace fhcp

#endif  // LIB_FHCP_CPP_FHCP_H_
