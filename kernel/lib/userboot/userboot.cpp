// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <lib/console.h>
#include <lk/init.h>
#include <platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <magenta/msg_pipe_dispatcher.h>
#include <magenta/process_dispatcher.h>
#include <magenta/processargs.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/vm_object_dispatcher.h>

#include "code-start.h"

static const size_t stack_size = 16 * PAGE_SIZE;

extern char __kernel_cmdline[CMDLINE_MAX];
extern unsigned __kernel_cmdline_size;
extern unsigned __kernel_cmdline_count;

// TODO(mcgrathr): Should go away when process lifetime stuff is fixed.
static utils::RefPtr<Dispatcher> userboot_process;

// These are defined in assembly by vdso.S; code-start.h
// gives details about their size and layout.
extern "C" const char vdso_image[], userboot_image[];


// Make a new VM object populated from some pages we have on hand.
static utils::RefPtr<VmObject> make_vmo_from_memory(const void* data,
                                                    size_t size) {
    auto vmo = VmObject::Create(PMM_ALLOC_FLAG_ANY, size);
    if (vmo && size > 0) {
        ASSERT((uintptr_t)data % PAGE_SIZE == 0);
        ASSERT(size % PAGE_SIZE == 0);
        // TODO(mcgrathr): Ideally this would steal the pages from the kernel
        // image rather than copying them into new space.  These pages will
        // never be used by the kernel again, so it's a waste to keep them in
        // core as part of the kernel image.
        size_t written;
        if (vmo->Write(data, 0, size, &written) < 0 || written != size)
            vmo.reset();
    }
    return vmo;
}

// Get a handle to a VM object, with full rights except perhaps for writing.
static mx_status_t get_vmo_handle(utils::RefPtr<VmObject> vmo, bool readonly,
                                  HandleUniquePtr* ptr) {
    if (!vmo)
        return ERR_NO_MEMORY;
    mx_rights_t rights;
    utils::RefPtr<Dispatcher> dispatcher;
    mx_status_t result = VmObjectDispatcher::Create(
        utils::move(vmo), &dispatcher, &rights);
    if (result == NO_ERROR) {
        if (readonly)
            rights &= ~MX_RIGHT_WRITE;
        *ptr = HandleUniquePtr(MakeHandle(utils::move(dispatcher), rights));
    }
    return result;
}

// Map a segment from one of our embedded VM objects.
// If *mapped_address is zero to begin with, it can go anywhere.
static mx_status_t map_dso_segment(utils::RefPtr<VmAspace> aspace,
                                   const char* name, bool code,
                                   utils::RefPtr<VmObject> vmo,
                                   uintptr_t start_offset,
                                   uintptr_t end_offset,
                                   uintptr_t* mapped_address) {
    char mapping_name[32];
    strlcpy(mapping_name, name, sizeof(mapping_name));
    strlcat(mapping_name, code ? "-code" : "-rodata", sizeof(mapping_name));

    uint vmm_flags = *mapped_address != 0 ? VMM_FLAG_VALLOC_SPECIFIC : 0;
    uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_READ;
    if (code)
        arch_mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;

    size_t len = end_offset - start_offset;
    mx_status_t status = aspace->MapObject(
        vmo, mapping_name, start_offset, len,
        reinterpret_cast<void**>(mapped_address),
        0, vmm_flags, arch_mmu_flags);

    if (status < 0) {
        dprintf(CRITICAL,
                "userboot: %s mapping %#" PRIxPTR " @ %#" PRIxPTR
                " size %#zx failed %d\n",
                mapping_name, start_offset, *mapped_address, len, status);
    } else {
        dprintf(SPEW, "userboot: %-16s %#6" PRIxPTR " @ [%#" PRIxPTR
                ",%#" PRIxPTR ")\n", mapping_name, start_offset,
                *mapped_address, *mapped_address + len);
    }

    return status;
}

// Map one of our embedded DSOs from its VM object.
// If *start_address is zero, it can go anywhere.
static mx_status_t map_dso_image(const char* name,
                                 utils::RefPtr<VmAspace> aspace,
                                 uintptr_t* start_address,
                                 utils::RefPtr<VmObject> vmo,
                                 size_t code_start, size_t code_end) {
    ASSERT(*start_address % PAGE_SIZE == 0);
    ASSERT(code_start % PAGE_SIZE == 0);
    ASSERT(code_start > 0);
    ASSERT(code_end % PAGE_SIZE == 0);
    ASSERT(code_end > code_start);

    mx_status_t status = map_dso_segment(aspace, name, false, vmo,
                                         0, code_start, start_address);
    if (status == 0) {
        uintptr_t code_address = *start_address + code_start;
        status = map_dso_segment(aspace, name, true, vmo,
                                 code_start, code_end, &code_address);
    }

    return status;
}

// Create a message pipe and write the bootstrap message down one side of
// it, returning the handle to the other side.
static HandleUniquePtr make_bootstrap_pipe(
    const void* bytes, uint32_t num_bytes,
    HandleUniquePtr handles[], uint32_t num_handles) {
    HandleUniquePtr user_pipe_handle;
    utils::RefPtr<MessagePipeDispatcher> kernel_pipe;
    {
        utils::RefPtr<Dispatcher> mpd0, mpd1;
        mx_rights_t rights;
        status_t status = MessagePipeDispatcher::Create(
            0, &mpd0, &mpd1, &rights);
        if (status != NO_ERROR)
            return nullptr;
        user_pipe_handle.reset(MakeHandle(utils::move(mpd0), rights));
        kernel_pipe.reset(mpd1->get_message_pipe_dispatcher());
    }
    if (!user_pipe_handle)
        return nullptr;
    ASSERT(kernel_pipe);

    // Now pack up the bytes and handles to write down the pipe.
    AllocChecker ac;
    utils::Array<uint8_t> buffer(new (&ac) uint8_t[num_bytes], num_bytes);
    if (!ac.check())
        return nullptr;
    memcpy(buffer.get(), bytes, buffer.size());

    utils::Array<Handle*> handle_list(new (&ac) Handle*[num_handles],
                                      num_handles);
    if (!ac.check())
        return nullptr;
    for (uint32_t i = 0; i < num_handles; ++i)
        handle_list[i] = handles[i].release();

    // Here it goes!
    mx_status_t status = kernel_pipe->Write(utils::move(buffer),
                                            utils::move(handle_list));
    if (status != NO_ERROR)
        return nullptr;

    return user_pipe_handle;
}

enum bootstrap_handle_index {
    BOOTSTRAP_VDSO,
    BOOTSTRAP_BOOTFS,
    BOOTSTRAP_RAMDISK,
    BOOTSTRAP_STACK,
    //BOOTSTRAP_PROC, TODO(mcgrathr): later
    //BOOTSTRAP_THREAD, TODO(mcgrathr): later
    BOOTSTRAP_HANDLES
};

struct bootstrap_message {
    mx_proc_args_t header;
    uint32_t handle_info[BOOTSTRAP_HANDLES];
    char cmdline[CMDLINE_MAX];
};

static uint32_t prepare_bootstrap_msg(struct bootstrap_message* msg) {
    memset(&msg->header, 0, sizeof(msg->header));
    msg->header.protocol = MX_PROCARGS_PROTOCOL;
    msg->header.version = MX_PROCARGS_VERSION;
    msg->header.environ_off = offsetof(struct bootstrap_message, cmdline);
    msg->header.environ_num = __kernel_cmdline_count;
    msg->header.handle_info_off =
        offsetof(struct bootstrap_message, handle_info);
    for (int i = 0; i < BOOTSTRAP_HANDLES; ++i) {
        uint32_t info = 0;
        switch (static_cast<bootstrap_handle_index>(i)) {
        case BOOTSTRAP_VDSO:
            info = MX_HND_INFO(MX_HND_TYPE_VDSO_VMO, 0);
            break;
        case BOOTSTRAP_BOOTFS:
            info = MX_HND_INFO(MX_HND_TYPE_BOOTFS_VMO, 0);
            break;
        case BOOTSTRAP_RAMDISK:
            info = MX_HND_INFO(MX_HND_TYPE_BOOTFS_VMO, 1);
            break;
#if 0 // TODO(mcgrathr): later
        case BOOTSTRAP_PROC:
            info = MX_HND_INFO(MX_HND_TYPE_PROC_SELF, 0);
            break;
        case BOOTSTRAP_THREAD:
            info = MX_HND_INFO(MX_HND_TYPE_THREAD_SELF, 0);
            break;
#endif
        case BOOTSTRAP_STACK:
            info = MX_HND_INFO(MX_HND_TYPE_STACK_VMO, 0);
            break;
        case BOOTSTRAP_HANDLES:
            __builtin_unreachable();
        }
        msg->handle_info[i] = info;
    }
    memcpy(msg->cmdline, __kernel_cmdline, __kernel_cmdline_size);
    return static_cast<uint32_t>(offsetof(struct bootstrap_message, cmdline) +
                                 __kernel_cmdline_size);
}

static int attempt_userboot(const void* bootfs, size_t bfslen) {
    dprintf(INFO, "userboot: bootfs %16zu @ %p\n", bfslen, bootfs);

    size_t rsize;
    void* rbase = platform_get_ramdisk(&rsize);
    if (rbase)
        dprintf(INFO, "userboot: ramdisk %15zu @ %p\n", rsize, rbase);

    auto vdso_vmo = make_vmo_from_memory(vdso_image, VDSO_CODE_END);
    auto userboot_vmo = make_vmo_from_memory(userboot_image,
                                             USERBOOT_CODE_END);
    auto stack_vmo = VmObject::Create(PMM_ALLOC_FLAG_ANY, stack_size);
    if (!vdso_vmo || !userboot_vmo || !stack_vmo)
        return ERR_NO_MEMORY;

    HandleUniquePtr handles[BOOTSTRAP_HANDLES];
    mx_status_t status = get_vmo_handle(make_vmo_from_memory(bootfs, bfslen),
                                        false, &handles[BOOTSTRAP_BOOTFS]);
    if (status == NO_ERROR)
        status = get_vmo_handle(make_vmo_from_memory(rbase, rsize),
                                false, &handles[BOOTSTRAP_RAMDISK]);
    if (status == NO_ERROR)
        status = get_vmo_handle(vdso_vmo, true, &handles[BOOTSTRAP_VDSO]);
    if (status == NO_ERROR)
        status = get_vmo_handle(stack_vmo, false, &handles[BOOTSTRAP_STACK]);
    if (status != NO_ERROR)
        return status;

    mx_rights_t rights;
    utils::RefPtr<Dispatcher> proc_disp;
    status = ProcessDispatcher::Create("userboot", &proc_disp, &rights);
    if (status < 0)
        return status;

    auto proc = proc_disp->get_process_dispatcher();
    auto aspace = proc->aspace();

    // Map the userboot image anywhere.
    uintptr_t userboot_base = 0;
    status = map_dso_image(
        "userboot", aspace, &userboot_base, userboot_vmo,
        USERBOOT_CODE_START, USERBOOT_CODE_END);
    if (status < 0)
        return status;
    uintptr_t entry = userboot_base + USERBOOT_ENTRY;
    // TODO(mcgrathr): The rodso-code.sh script uses nm, which lies
    // about the actual ELF symbol values when they are Thumb function
    // symbols with the low bit set (it clears the low bit in what it
    // displays).  We assume that if the kernel is built as Thumb,
    // userboot's entry point will be too, and Thumbify it.
#ifdef __thumb__
    entry |= 1;
#endif

    // Map the vDSO image immediately after the userboot image, where the
    // userboot code expects to find it.  We assume that ASLR won't have
    // placed the userboot image so close to the top of the address space
    // that there isn't any room left for the vDSO.
    uintptr_t vdso_base = userboot_base + USERBOOT_CODE_END;
    status = map_dso_image("vdso", aspace, &vdso_base, vdso_vmo,
                           VDSO_CODE_START, VDSO_CODE_END);
    if (status < 0)
        return status;

    // Map the stack anywhere.
    uintptr_t sp;
    {
        void* ptr;
        status = aspace->MapObject(
            stack_vmo, "stack", 0, stack_size, &ptr,
            0, 0, ARCH_MMU_FLAG_PERM_USER |
            ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
        if (status < 0)
            return status;
        sp = (uintptr_t)ptr + stack_size;
#ifdef __x86_64__
        // The x86-64 ABI requires %rsp % 16 = 8 on entry.  The zero word
        // at (%rsp) serves as the return address for the outermost frame.
        sp -= 8;
#elif defined(__arm__) || defined(__aarch64__)
        // The ARMv7 and ARMv8 ABIs both just require that SP be aligned.
#else
# error what machine?
#endif
    }

#if 0 // TODO(mcgrathr): later
    // Create the user thread and stash its handle for the bootstrap message.
    ThreadDispatcher* thread;
    {
        utils::RefPtr<UserThread> ut;
        status = proc->CreateUserThread("userboot", &ut);
        if (status < 0)
            return status;
        utils::RefPtr<Dispatcher> ut_disp;
        status = ThreadDispatcher::Create(ut, &ut_disp, &rights);
        if (status < 0)
            return status;
        thread = ut_disp->get_thread_dispatcher();
        handles[BOOTSTRAP_THREAD] = MakeHandle(utils::move(ut_disp), rights);
    }
    DEBUG_ASSERT(thread);
#endif

    mx_handle_t hv;
    {
        // The bootstrap message buffer is too big to fit on a kernel
        // stack, so allocate it.
        AllocChecker ac;
        utils::unique_ptr<bootstrap_message> msg(new (&ac) bootstrap_message);
        if (!ac.check())
            return ERR_NO_MEMORY;

        uint32_t msg_size = prepare_bootstrap_msg(msg.get());
        auto handle = make_bootstrap_pipe(
            msg.get(), msg_size,
            handles, static_cast<uint32_t>(BOOTSTRAP_HANDLES));
        if (!handle)
            return ERR_NO_MEMORY;

        hv = proc->MapHandleToValue(handle.get());
        proc->AddHandle(utils::move(handle));
    }

    dprintf(SPEW, "userboot: %-23s @ %#" PRIxPTR "\n", "entry point", entry);
#if 0 // TODO(mcgrathr): later
    status = proc->Start(thread, entry, sp, hv);
#else
    (void)sp;
    status = proc->Start((void*)(uintptr_t)hv, entry);
#endif
    if (status != NO_ERROR) {
        printf("userboot: failed to start process %d\n", status);
        return status;
    }

    // hold onto a global ref to the boot process.
    // TODO(mcgrathr): This should go away eventually.
    userboot_process = utils::move(proc_disp);
    return NO_ERROR;
}

#if EMBED_USER_BOOTFS
extern "C" const uint8_t user_bootfs[];
extern "C" const uint32_t user_bootfs_len;

void userboot_init(uint level) {
#if !WITH_APP_SHELL
    dprintf(INFO, "userboot: console init\n");
    console_init();
#endif

    attempt_userboot(user_bootfs, user_bootfs_len);
}

LK_INIT_HOOK(userboot, userboot_init, LK_INIT_LEVEL_APPS - 1);
#endif
