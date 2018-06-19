// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <trace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/cmdline.h>
#include <vm/vm_object_paged.h>
#include <lib/console.h>
#include <lib/vdso.h>
#include <lk/init.h>
#include <mexec.h>
#include <object/channel_dispatcher.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <zircon/processargs.h>
#include <zircon/stack.h>

#if ENABLE_ENTROPY_COLLECTOR_TEST
#include <lib/crypto/entropy/quality_test.h>
#endif

static const size_t stack_size = ZIRCON_DEFAULT_STACK_SIZE;

#define STACK_VMO_NAME "userboot-initial-stack"
#define RAMDISK_VMO_NAME "userboot-raw-ramdisk"
#define CRASHLOG_VMO_NAME "crashlog"

namespace {

#include "userboot-code.h"

// This is defined in assembly by userboot-image.S; userboot-code.h
// gives details about the image's size and layout.
extern "C" const char userboot_image[];

class UserbootImage : private RoDso {
public:
    explicit UserbootImage(const VDso* vdso)
        : RoDso("userboot", userboot_image,
                USERBOOT_CODE_END, USERBOOT_CODE_START),
          vdso_(vdso) {}

    // The whole userboot image consists of the userboot rodso image
    // immediately followed by the vDSO image.  This returns the size
    // of that combined image.
    size_t size() const {
        return RoDso::size() + vdso_->size();
    }

    zx_status_t Map(fbl::RefPtr<VmAddressRegionDispatcher> root_vmar,
                    uintptr_t* vdso_base, uintptr_t* entry) {
        // Create a VMAR (placed anywhere) to hold the combined image.
        fbl::RefPtr<VmAddressRegionDispatcher> vmar;
        zx_rights_t vmar_rights;
        zx_status_t status = root_vmar->Allocate(0, size(),
                                                 ZX_VM_FLAG_CAN_MAP_READ |
                                                 ZX_VM_FLAG_CAN_MAP_WRITE |
                                                 ZX_VM_FLAG_CAN_MAP_EXECUTE |
                                                 ZX_VM_FLAG_CAN_MAP_SPECIFIC,
                                                 &vmar, &vmar_rights);
        if (status != ZX_OK)
            return status;

        // Map userboot proper.
        status = RoDso::Map(vmar, 0);
        if (status == ZX_OK) {
            *entry = vmar->vmar()->base() + USERBOOT_ENTRY;

            // Map the vDSO right after it.
            *vdso_base = vmar->vmar()->base() + RoDso::size();
            status = vdso_->Map(fbl::move(vmar), RoDso::size());
        }
        return status;
    }

private:
    const VDso* vdso_;
};

} // anonymous namespace


// Get a handle to a VM object, with full rights except perhaps for writing.
static zx_status_t get_vmo_handle(fbl::RefPtr<VmObject> vmo, bool readonly,
                                  fbl::RefPtr<VmObjectDispatcher>* disp_ptr,
                                  Handle** ptr) {
    if (!vmo)
        return ZX_ERR_NO_MEMORY;
    zx_rights_t rights;
    fbl::RefPtr<Dispatcher> dispatcher;
    zx_status_t result = VmObjectDispatcher::Create(
        fbl::move(vmo), &dispatcher, &rights);
    if (result == ZX_OK) {
        if (disp_ptr)
            *disp_ptr = fbl::RefPtr<VmObjectDispatcher>::Downcast(dispatcher);
        if (readonly)
            rights &= ~ZX_RIGHT_WRITE;
        if (ptr)
            *ptr = Handle::Make(fbl::move(dispatcher), rights).release();
    }
    return result;
}

static zx_status_t get_job_handle(Handle** ptr) {
    zx_rights_t rights;
    fbl::RefPtr<Dispatcher> dispatcher;
    zx_status_t result = JobDispatcher::Create(
        0u, GetRootJobDispatcher(), &dispatcher, &rights);
    if (result == ZX_OK)
        *ptr = Handle::Make(fbl::move(dispatcher), rights).release();
    return result;
}

static zx_status_t get_resource_handle(Handle** ptr) {
    zx_rights_t rights;
    fbl::RefPtr<ResourceDispatcher> root;
    zx_status_t result = ResourceDispatcher::Create(&root, &rights, ZX_RSRC_KIND_ROOT, 0, 0);
    if (result == ZX_OK)
        *ptr = Handle::Make(fbl::RefPtr<Dispatcher>(root.get()),
                            rights).release();
    return result;
}

// Create a channel and write the bootstrap message down one side of
// it, returning the handle to the other side.
static zx_status_t make_bootstrap_channel(
    fbl::RefPtr<ProcessDispatcher> process,
    fbl::unique_ptr<MessagePacket> msg,
    zx_handle_t* out) {
    HandleOwner user_channel_handle;
    fbl::RefPtr<ChannelDispatcher> kernel_channel;
    *out = ZX_HANDLE_INVALID;
    {
        fbl::RefPtr<Dispatcher> mpd0, mpd1;
        zx_rights_t rights;
        zx_status_t status = ChannelDispatcher::Create(&mpd0, &mpd1, &rights);
        if (status != ZX_OK)
            return status;
        user_channel_handle = Handle::Make(fbl::move(mpd0), rights);
        kernel_channel = DownCastDispatcher<ChannelDispatcher>(&mpd1);
    }

    // Here it goes!
    zx_status_t status = kernel_channel->Write(fbl::move(msg));
    if (status != ZX_OK)
        return status;

    zx_handle_t hv = process->MapHandleToValue(user_channel_handle);
    process->AddHandle(fbl::move(user_channel_handle));

    *out = hv;
    return ZX_OK;
}

enum bootstrap_handle_index {
    BOOTSTRAP_VDSO,
    BOOTSTRAP_VDSO_LAST_VARIANT = BOOTSTRAP_VDSO + VDso::variants() - 1,
    BOOTSTRAP_RAMDISK,
    BOOTSTRAP_RESOURCE_ROOT,
    BOOTSTRAP_STACK,
    BOOTSTRAP_PROC,
    BOOTSTRAP_THREAD,
    BOOTSTRAP_JOB,
    BOOTSTRAP_VMAR_ROOT,
    BOOTSTRAP_CRASHLOG,
#if ENABLE_ENTROPY_COLLECTOR_TEST
    BOOTSTRAP_ENTROPY_FILE,
#endif
    BOOTSTRAP_HANDLES
};

struct bootstrap_message {
    zx_proc_args_t header;
    uint32_t handle_info[BOOTSTRAP_HANDLES];
    char cmdline[CMDLINE_MAX];
};

static fbl::unique_ptr<MessagePacket> prepare_bootstrap_message() {
    const size_t data_size =
        offsetof(struct bootstrap_message, cmdline) +
        __kernel_cmdline_size;
    bootstrap_message* msg =
        static_cast<bootstrap_message*>(malloc(data_size));
    if (msg == nullptr) {
        return nullptr;
    }

    memset(&msg->header, 0, sizeof(msg->header));
    msg->header.protocol = ZX_PROCARGS_PROTOCOL;
    msg->header.version = ZX_PROCARGS_VERSION;
    msg->header.environ_off = offsetof(struct bootstrap_message, cmdline);
    msg->header.environ_num = static_cast<uint32_t>(__kernel_cmdline_count);
    msg->header.handle_info_off =
        offsetof(struct bootstrap_message, handle_info);
    for (int i = 0; i < BOOTSTRAP_HANDLES; ++i) {
        uint32_t info = 0;
        switch (static_cast<bootstrap_handle_index>(i)) {
        case BOOTSTRAP_VDSO ... BOOTSTRAP_VDSO_LAST_VARIANT:
            info = PA_HND(PA_VMO_VDSO, i - BOOTSTRAP_VDSO);
            break;
        case BOOTSTRAP_RAMDISK:
            info = PA_HND(PA_VMO_BOOTDATA, 0);
            break;
        case BOOTSTRAP_RESOURCE_ROOT:
            info = PA_HND(PA_RESOURCE, 0);
            break;
        case BOOTSTRAP_STACK:
            info = PA_HND(PA_VMO_STACK, 0);
            break;
        case BOOTSTRAP_PROC:
            info = PA_HND(PA_PROC_SELF, 0);
            break;
        case BOOTSTRAP_THREAD:
            info = PA_HND(PA_THREAD_SELF, 0);
            break;
        case BOOTSTRAP_JOB:
            info = PA_HND(PA_JOB_DEFAULT, 0);
            break;
        case BOOTSTRAP_VMAR_ROOT:
            info = PA_HND(PA_VMAR_ROOT, 0);
            break;
        case BOOTSTRAP_CRASHLOG:
            info = PA_HND(PA_VMO_KERNEL_FILE, 0);
            break;
#if ENABLE_ENTROPY_COLLECTOR_TEST
        case BOOTSTRAP_ENTROPY_FILE:
            info = PA_HND(PA_VMO_KERNEL_FILE, 1);
            break;
#endif
        case BOOTSTRAP_HANDLES:
            __builtin_unreachable();
        }
        msg->handle_info[i] = info;
    }
    memcpy(msg->cmdline, __kernel_cmdline, __kernel_cmdline_size);

    fbl::unique_ptr<MessagePacket> packet;
    uint32_t num_handles = BOOTSTRAP_HANDLES;
    zx_status_t status =
        MessagePacket::Create(msg, static_cast<uint32_t>(data_size), num_handles, &packet);
    free(msg);
    if (status != ZX_OK) {
        return nullptr;
    }
    return packet;
}

static void clog_to_vmo(const void* data, size_t off, size_t len, void* cookie) {
    VmObject* vmo = static_cast<VmObject*>(cookie);
    vmo->Write(data, off, len);
}

// Converts platform crashlog into a VMO
static zx_status_t crashlog_to_vmo(fbl::RefPtr<VmObject>* out) {
    size_t size = platform_recover_crashlog(0, NULL, NULL);
    fbl::RefPtr<VmObject> crashlog_vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, size, &crashlog_vmo);
    if (status != ZX_OK) {
        return status;
    }
    platform_recover_crashlog(size, crashlog_vmo.get(), clog_to_vmo);
    crashlog_vmo->set_name(CRASHLOG_VMO_NAME, sizeof(CRASHLOG_VMO_NAME) - 1);
    mexec_stash_crashlog(crashlog_vmo);
    *out = fbl::move(crashlog_vmo);
    return ZX_OK;
}

static zx_status_t attempt_userboot() {
    size_t rsize;
    void* rbase = platform_get_ramdisk(&rsize);
    if (rbase)
        dprintf(INFO, "userboot: ramdisk %#15zx @ %p\n", rsize, rbase);

    fbl::RefPtr<VmObject> stack_vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, stack_size, &stack_vmo);
    if (status != ZX_OK)
        return status;
    stack_vmo->set_name(STACK_VMO_NAME, sizeof(STACK_VMO_NAME) - 1);

    fbl::RefPtr<VmObject> rootfs_vmo;
    status = VmObjectPaged::CreateFromROData(rbase, rsize, &rootfs_vmo);
    if (status != ZX_OK)
        return status;
    rootfs_vmo->set_name(RAMDISK_VMO_NAME, sizeof(RAMDISK_VMO_NAME) - 1);

    fbl::RefPtr<VmObject> crashlog_vmo;
    status = crashlog_to_vmo(&crashlog_vmo);
    if (status != ZX_OK)
        return status;

    // Prepare the bootstrap message packet.  This puts its data (the
    // kernel command line) in place, and allocates space for its handles.
    // We'll fill in the handles as we create things.
    fbl::unique_ptr<MessagePacket> msg = prepare_bootstrap_message();
    if (!msg)
        return ZX_ERR_NO_MEMORY;

    Handle** const handles = msg->mutable_handles();
    DEBUG_ASSERT(msg->num_handles() == BOOTSTRAP_HANDLES);
    status = get_vmo_handle(rootfs_vmo, false, nullptr,
                            &handles[BOOTSTRAP_RAMDISK]);
    fbl::RefPtr<VmObjectDispatcher> stack_vmo_dispatcher;
    if (status == ZX_OK)
        status = get_vmo_handle(stack_vmo, false, &stack_vmo_dispatcher,
                                &handles[BOOTSTRAP_STACK]);
    if (status == ZX_OK)
        status = get_vmo_handle(crashlog_vmo, true, nullptr,
                                &handles[BOOTSTRAP_CRASHLOG]);
    if (status == ZX_OK)
        status = get_resource_handle(&handles[BOOTSTRAP_RESOURCE_ROOT]);

    if (status == ZX_OK)
        status = get_job_handle(&handles[BOOTSTRAP_JOB]);

#if ENABLE_ENTROPY_COLLECTOR_TEST
    if (status == ZX_OK) {
        if (crypto::entropy::entropy_was_lost) {
            status = ZX_ERR_INTERNAL;
        } else {
            status = get_vmo_handle(
                    crypto::entropy::entropy_vmo,
                    /* readonly */ true, /* disp_ptr */ nullptr,
                    &handles[BOOTSTRAP_ENTROPY_FILE]);
        }
    }
#endif
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<Dispatcher> proc_disp;
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_rights_t rights, vmar_rights;
    status = ProcessDispatcher::Create(GetRootJobDispatcher(), "userboot", 0,
                                       &proc_disp, &rights,
                                       &vmar, &vmar_rights);
    if (status < 0)
        return status;

    handles[BOOTSTRAP_PROC] = Handle::Make(proc_disp, rights).release();

    auto proc = DownCastDispatcher<ProcessDispatcher>(&proc_disp);
    ASSERT(proc);

    handles[BOOTSTRAP_VMAR_ROOT] = Handle::Make(vmar, vmar_rights).release();

    const VDso* vdso = VDso::Create();
    for (size_t i = BOOTSTRAP_VDSO; i <= BOOTSTRAP_VDSO_LAST_VARIANT; ++i) {
        HandleOwner vmo_handle =
            vdso->vmo_handle(static_cast<VDso::Variant>(i - BOOTSTRAP_VDSO));
        handles[i] = vmo_handle.release();
    }

    UserbootImage userboot(vdso);
    uintptr_t vdso_base = 0;
    uintptr_t entry = 0;
    status = userboot.Map(vmar, &vdso_base, &entry);
    if (status != ZX_OK)
        return status;

    // Map the stack anywhere.
    fbl::RefPtr<VmMapping> stack_mapping;
    status = vmar->Map(0,
                       fbl::move(stack_vmo), 0, stack_size,
                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                       &stack_mapping);
    if (status != ZX_OK)
        return status;

    uintptr_t stack_base = stack_mapping->base();
    uintptr_t sp = compute_initial_stack_pointer(stack_base, stack_size);

    // Create the user thread and stash its handle for the bootstrap message.
    fbl::RefPtr<ThreadDispatcher> thread;
    {
        fbl::RefPtr<Dispatcher> ut_disp;
        // Make a copy of proc, as we need to a keep a copy for the
        // bootstrap message.
        status = ThreadDispatcher::Create(proc, 0, "userboot", &ut_disp, &rights);
        if (status < 0)
            return status;
        handles[BOOTSTRAP_THREAD] = Handle::Make(ut_disp, rights).release();
        thread = DownCastDispatcher<ThreadDispatcher>(&ut_disp);
    }
    DEBUG_ASSERT(thread);

    // All the handles are in place, so we can send the bootstrap message.
    zx_handle_t hv;
    status = make_bootstrap_channel(proc, fbl::move(msg), &hv);
    if (status != ZX_OK)
        return status;

    dprintf(SPEW, "userboot: %-23s @ %#" PRIxPTR "\n", "entry point", entry);

    // Start the process's initial thread.
    status = thread->Start(entry, sp, static_cast<uintptr_t>(hv), vdso_base,
                           /* initial_thread= */ true);
    if (status != ZX_OK) {
        printf("userboot: failed to start initial thread: %d\n", status);
        return status;
    }

    return ZX_OK;
}

void userboot_init(uint level) {
    attempt_userboot();
}

LK_INIT_HOOK(userboot, userboot_init, LK_INIT_LEVEL_USER);
