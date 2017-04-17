// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGENTA_PLATFORM_LAUNCHER_H
#define MAGENTA_PLATFORM_LAUNCHER_H

#include <magenta/types.h>

namespace magma {

class MagentaPlatformLauncher {
public:
    static bool Launch(mx_handle_t job, const char* name, int argc, const char* const* argv,
                       const char* envp[], mx_handle_t* handles, uint32_t* types, size_t hcount);
};

} // namespace

#endif // MAGENTA_PLATFORM_LAUNCHER_H
