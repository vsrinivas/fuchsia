// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "main.h"

#include <zircon/compiler.h>

__BEGIN_CDECLS

int main(int argc, char** argv) { devmgr_device_host_main(argc, argv); }

// All drivers have a pure C ABI.  But each individual driver might statically
// link in its own copy of some C++ library code.  Since no C++ language
// relationships leak through the driver ABI, each driver is its own whole
// program from the perspective of the C++ language rules.  But the ASan
// runtime doesn't understand this and wants to diagnose ODR violations when
// the same global is defined in multiple drivers, which is likely with C++
// library use.  There is no real way to teach the ASan instrumentation or
// runtime about symbol visibility and isolated worlds within the program, so
// the only thing to do is suppress the ODR violation detection.  This
// unfortunately means real ODR violations within a single C++ driver won't be
// caught either.
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
const char* __asan_default_options() { return "detect_odr_violation=0"; }
#endif

__END_CDECLS
