// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//

#include <lib/capsule.h>

int32_t capsule_store(uint8_t tag, void* capsule, uint32_t size) {
    return 0;
}

int32_t capsule_fetch(uint8_t tag, void* capsule, uint32_t size) {
    return count;
}

#if WITH_LIB_CONSOLE

static int cmd_capsule(int argc, const cmd_args *argv, uint32_t flags) {
    printf("capsule not implemented for ARM\n");
    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("capsule", "query or dump capsule", &cmd_capsule)
STATIC_COMMAND_END(capsule);

#endif  // WITH_LIB_CONSOLE
