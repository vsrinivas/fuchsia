// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#ifdef WITH_LIB_CONSOLE
#include <debug.h>
#include <dev/hw_rng.h>
#include <lib/console.h>
#include <stdlib.h>

static int cmd_rng32(int argc, const cmd_args *argv, uint32_t flags)
{
    uint32_t val = hw_rng_get_u32();
    printf("Random val = %u (0x%08x)\n", val, val);
    return MX_OK;
}

static int cmd_rng(int argc, const cmd_args *argv, uint32_t flags)
{
    if ((argc < 2) || (argc > 3)) {
        printf("Invalid argument count\n\n"
               "Usage : %s <N> [wait]\n"
               "N     : Number of bytes to generate.\n"
               "wait  : true  -> wait indefinitely for bytes to be generated\n"
               "      : false -> terminate if HW generator runs out of entropy (default)\n",
               argv[0].str);
        return MX_ERR_INVALID_ARGS;
    }

    printf("Generating %lu random bytes\n", argv[1].u);

    size_t offset = 0;
    bool wait = (argc == 3) ? argv[2].b : false;
    while (offset < argv[1].u) {
        uint8_t bytes[16];
        size_t todo, done;

        todo = MIN(sizeof(bytes), argv[1].u - offset);
        done = hw_rng_get_entropy(bytes, todo, wait);
        DEBUG_ASSERT(done <= todo);

        hexdump8_ex(bytes, done, offset);
        offset += done;

        if (done < todo) {
            printf("Entropy exhausted after %zu byte%s\n",
                    offset, offset == 1 ? "" : "s");
            break;
        }
    }

    return MX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("rng32",
               "Generate and print a random 32 bit unsigned integer using the HW RNG",
               &cmd_rng32)
STATIC_COMMAND("rng",
               "Generate and print N random bytes using the HW RNG",
               &cmd_rng)
STATIC_COMMAND_END(hw_rng);

#endif  // WITH_LIB_CONSOLE
