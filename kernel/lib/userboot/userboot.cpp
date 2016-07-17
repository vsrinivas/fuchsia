// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <lib/console.h>
#include <lib/elf.h>
#include <lk/init.h>
#include <magenta/user_process.h>
#include <magenta/vm_object_dispatcher.h>
#include <platform.h>
#include <stdio.h>
#include <trace.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/user_process.h>
#include <magenta/vm_object_dispatcher.h>

#define USERBOOT_PAYLOAD 0x2000000

static int join_thread(void* arg) {
    //UserProcess* proc = (UserProcess*)arg;

    // TODO: block on the process in some other way
    //proc->Join(nullptr);
    //delete proc;
    return 0;
}

extern char __kernel_cmdline[CMDLINE_MAX];

static utils::RefPtr<UserProcess> userboot_process;

static int attempt_userboot(const void* userboot, size_t ublen,
                            const void* bootfs, size_t bfslen) {
    dprintf(INFO, "userboot: bootfs   @%p (%zd)\n", bootfs, bfslen);
    dprintf(INFO, "userboot: userboot @%p (%zd)\n", userboot, ublen);

    size_t rsize;
    void *rbase = platform_get_ramdisk(&rsize);

    if (rbase) {
        dprintf(INFO, "userboot: ramdisk  @%p (%zd)\n", rbase, rsize);
    }

    auto proc = utils::AdoptRef(new UserProcess("userboot"));
    if (!proc) {
        return ERR_NO_MEMORY;
    }
    status_t err = proc->Initialize();
    if (err != NO_ERROR) {
        return err;
    }

    auto aspace = proc->aspace();

    ElfLoader::MemFile loader("userboot", aspace, userboot, ublen);
    if ((err = loader.Load()) != NO_ERROR) {
        return err;
    }

    // map bootfs into the userboot process
    auto vmo = VmObject::Create(PMM_ALLOC_FLAG_ANY, bfslen + rsize + CMDLINE_MAX);
    if (!vmo) {
        return ERR_NO_MEMORY;
    }

    size_t written;
    if (vmo->Write(__kernel_cmdline, 0, CMDLINE_MAX, &written) < 0) {
        return ERR_NO_MEMORY;
    }
    if (vmo->Write(bootfs, CMDLINE_MAX, bfslen, &written) < 0) {
        return ERR_NO_MEMORY;
    }

    if (rbase) {
        if (vmo->Write(rbase, CMDLINE_MAX + bfslen, rsize, &written) < 0) {
            return ERR_NO_MEMORY;
        }
    }

    // create a Vm Object dispatcher
    utils::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = VmObjectDispatcher::Create(utils::move(vmo), &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    // create a handle and attach the dispatcher to it
    HandleUniquePtr handle(MakeHandle(utils::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    mx_handle_t hv = proc->MapHandleToValue(handle.get());
    proc->AddHandle(utils::move(handle));

    err = proc->Start((void*)(uintptr_t)hv, loader.entry());
    if (err != NO_ERROR) {
        printf("userboot: failed to start process %d\n", err);
        return err;
    }

    // create a thread to Join and delete the UserProcess so we don't leak
    thread_detach_and_resume(thread_create("join_thread", &join_thread, proc.get(),
                                           DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));

    // hold onto a global ref to this
    userboot_process = utils::move(proc);

    return NO_ERROR;
}

#if EMBED_USER_BOOTFS
extern "C" const uint8_t user_bootfs[];
extern "C" const uint32_t user_bootfs_len;

#define FSMAGIC "[BOOTFS]\0\0\0\0\0\0\0\0"

// bootfs contains multiple entries, but the first one
// must be the userboot binary, so we only need to be
// concerned with it
typedef struct bootfs {
    uint8_t magic[16];
    uint32_t namelen;
    uint32_t size;
    uint32_t offset;
} bootfs_t;

void userboot_init(uint level) {
    bootfs_t* bootfs = (bootfs_t*)user_bootfs;

#if !WITH_APP_SHELL
    dprintf(INFO, "userboot: console init\n");
    console_init();
#endif

    if (user_bootfs_len < sizeof(bootfs_t)) {
        return;
    }
    if (memcmp(bootfs->magic, FSMAGIC, 16)) {
        dprintf(INFO, "userboot: bad magic in bootfs\n");
        return;
    }
    if (bootfs->offset >= user_bootfs_len) {
        return;
    }
    if (bootfs->size > (user_bootfs_len - bootfs->offset)) {
        return;
    }
    attempt_userboot(user_bootfs + bootfs->offset, bootfs->size,
                     user_bootfs, user_bootfs_len);
}

LK_INIT_HOOK(userboot, userboot_init, LK_INIT_LEVEL_APPS - 1);
#endif
