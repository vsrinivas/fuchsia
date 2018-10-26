// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "devmgr.h"
#include "devcoordinator.h"
#include "log.h"

#include <driver-info/driver-info.h>

#include <zircon/driver/binding.h>
#include <zxcpp/new.h>

namespace devmgr {

struct AddContext {
    const char* libname;
    using Func = void(*)(Driver* drv, const char* version);
    Func func;
};

static bool is_driver_disabled(const char* name) {
    // driver.<driver_name>.disable
    char opt[16 + DRIVER_NAME_LEN_MAX];
    snprintf(opt, 16 + DRIVER_NAME_LEN_MAX, "driver.%s.disable", name);
    return getenv_bool(opt, false);
}

static void found_driver(zircon_driver_note_payload_t* note,
                         const zx_bind_inst_t* bi, void* cookie) {
    auto context = static_cast<const AddContext*>(cookie);

    // ensure strings are terminated
    note->name[sizeof(note->name) - 1] = 0;
    note->vendor[sizeof(note->vendor) - 1] = 0;
    note->version[sizeof(note->version) - 1] = 0;

    if (is_driver_disabled(note->name)) {
        return;
    }

    const char* libname = context->libname;

    if ((note->flags & ZIRCON_DRIVER_NOTE_FLAG_ASAN) && !dc_asan_drivers) {
        if (dc_launched_first_devhost) {
            log(ERROR, "%s (%s) requires ASan: cannot load after boot;"
                " consider devmgr.devhost.asan=true\n",
                libname, note->name);
            return;
        }
        dc_asan_drivers = true;
    }

    auto drv = fbl::make_unique<Driver>();
    if (drv == nullptr) {
        return;
    }

    auto binding = fbl::make_unique<zx_bind_inst_t[]>(note->bindcount);
    if (binding == nullptr) {
        return;
    }
    const size_t bindlen = note->bindcount * sizeof(zx_bind_inst_t);
    memcpy(binding.get(), bi, bindlen);
    drv->binding.reset(binding.release());
    drv->binding_size = static_cast<uint32_t>(bindlen);

    drv->libname.Set(libname);
    drv->name.Set(note->name);

#if VERBOSE_DRIVER_LOAD
    printf("found driver: %s\n", (char*) cookie);
    printf("        name: %s\n", note->name);
    printf("      vendor: %s\n", note->vendor);
    printf("     version: %s\n", note->version);
    printf("       flags: %#x\n", note->flags);
    printf("     binding:\n");
    for (size_t n = 0; n < note->bindcount; n++) {
        printf("         %03zd: %08x %08x\n", n, bi[n].op, bi[n].arg);
    }
#endif

    context->func(drv.release(), note->version);
}

void find_loadable_drivers(const char* path,
                           void (*func)(Driver* drv, const char* version)) {

    DIR* dir = opendir(path);
    if (dir == nullptr) {
        return;
    }
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        if (de->d_name[0] == '.') {
            continue;
        }
        if (de->d_type != DT_REG) {
            continue;
        }
        char libname[256 + 32];
        int r = snprintf(libname, sizeof(libname), "%s/%s", path, de->d_name);
        if ((r < 0) || (r >= (int)sizeof(libname))) {
            continue;
        }

        int fd;
        if ((fd = openat(dirfd(dir), de->d_name, O_RDONLY)) < 0) {
            continue;
        }
        AddContext context = { libname, func };
        zx_status_t status = di_read_driver_info(fd, &context, found_driver);
        close(fd);

        if (status) {
            if (status == ZX_ERR_NOT_FOUND) {
                printf("devcoord: no driver info in '%s'\n", libname);
            } else {
                printf("devcoord: error reading info from '%s'\n", libname);
            }
        }
    }
    closedir(dir);
}

void load_driver(const char* path,
                 void (*func)(Driver* drv, const char* version)) {
    //TODO: check for duplicate driver add
    int fd;
    if ((fd = open(path, O_RDONLY)) < 0) {
        printf("devcoord: cannot open '%s'\n", path);
        return;
    }

    AddContext context = { path, func };
    zx_status_t status = di_read_driver_info(fd, &context, found_driver);
    close(fd);

    if (status) {
        if (status == ZX_ERR_NOT_FOUND) {
            printf("devcoord: no driver info in '%s'\n", path);
        } else {
            printf("devcoord: error reading info from '%s'\n", path);
        }
    }
}

} // namespace devmgr
