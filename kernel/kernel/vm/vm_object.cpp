// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/vm/vm_object.h"

#include "vm_priv.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <lib/user_copy.h>
#include <new.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmObject::VmObject() {
}

VmObject::~VmObject() {
    // clear our magic value
    magic_ = 0;
}

static int cmd_vm_object(int argc, const cmd_args* argv) {
    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s dump <address>\n", argv[0].str);
        printf("%s dump_pages <address>\n", argv[0].str);
        return ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "dump")) {
        if (argc < 2)
            goto notenoughargs;

        VmObject* o = reinterpret_cast<VmObject*>(argv[2].u);

        o->Dump();
    } else if (!strcmp(argv[1].str, "dump_pages")) {
        if (argc < 2)
            goto notenoughargs;

        VmObject* o = reinterpret_cast<VmObject*>(argv[2].u);

        o->Dump(true);
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return NO_ERROR;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("vm_object", "vm object debug commands", &cmd_vm_object)
#endif
STATIC_COMMAND_END(vm_object);
