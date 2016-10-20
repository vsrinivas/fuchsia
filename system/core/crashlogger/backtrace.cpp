// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>

#include "backtrace.h"
#include "dso-list.h"
#include "utils.h"

static void btprint(dsoinfo_t* list, int n, uintptr_t pc, uintptr_t sp) {
    dsoinfo_t* dso = dso_lookup(list, pc);
    if (dso == nullptr) {
        fprintf(stderr, "bt#%02d: pc %p sp %p\n",
                n, (void*)pc, (void*)sp);
    } else {
        fprintf(stderr, "bt#%02d: pc %p sp %p (%s,%p)\n",
                n, (void*)pc, (void*)sp, dso->name, (void*)(pc - dso->base));
    }
}

void backtrace(mx_handle_t h, uintptr_t pc, uintptr_t fp) {
    dsoinfo_t* list = dso_fetch_list(h, "app");
    int n = 1;

    for (dsoinfo_t* dso = list; dso != nullptr; dso = dso->next) {
        printf("dso: id=%s base=%p name=%s\n",
               dso->buildid, (void*)dso->base, dso->name);
    }

    // N.B. This unwinder assumes code is compiled with -fno-omit-frame-pointer
    // and -mno-omit-leaf-frame-pointer on arm64.

    btprint(list, n++, pc, fp);
    while ((fp >= 0x1000000) && (n < 50)) {
        if (read_mem(h, fp + 8, &pc, sizeof(pc))) {
            break;
        }
        btprint(list, n++, pc, fp);
        if (read_mem(h, fp, &fp, sizeof(fp))) {
            break;
        }
    }
    fprintf(stderr, "bt#%02d: end\n", n);

    dso_free_list(list);
}
