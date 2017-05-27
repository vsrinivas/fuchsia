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
#include <kernel/vm/vm_object_paged.h>

#include <lib/console.h>
#include <lib/vdso.h>
#include <lk/init.h>

#include <magenta/channel_dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/job_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/message_packet.h>
#include <magenta/process_dispatcher.h>
#include <magenta/processargs.h>
#include <magenta/resource_dispatcher.h>
#include <magenta/stack.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/vm_address_region_dispatcher.h>
#include <magenta/vm_object_dispatcher.h>

static const size_t stack_size = MAGENTA_DEFAULT_STACK_SIZE;

extern char __kernel_cmdline[CMDLINE_MAX];
extern unsigned __kernel_cmdline_size;
extern unsigned __kernel_cmdline_count;

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

    mx_status_t Map(mxtl::RefPtr<VmAddressRegionDispatcher> root_vmar,
                    uintptr_t* vdso_base, uintptr_t* entry) {
        // Create a VMAR (placed anywhere) to hold the combined image.
        mxtl::RefPtr<VmAddressRegionDispatcher> vmar;
        mx_rights_t vmar_rights;
        mx_status_t status = root_vmar->Allocate(0, size(),
                                                 MX_VM_FLAG_CAN_MAP_READ |
                                                 MX_VM_FLAG_CAN_MAP_WRITE |
                                                 MX_VM_FLAG_CAN_MAP_EXECUTE |
                                                 MX_VM_FLAG_CAN_MAP_SPECIFIC,
                                                 &vmar, &vmar_rights);
        if (status != NO_ERROR)
            return status;

        // Map userboot proper.
        status = RoDso::Map(vmar, 0);
        if (status == NO_ERROR) {
            *entry = vmar->vmar()->base() + USERBOOT_ENTRY;
            // TODO(mcgrathr): The rodso-code.sh script uses nm, which lies
            // about the actual ELF symbol values when they are Thumb function
            // symbols with the low bit set (it clears the low bit in what it
            // displays).  We assume that if the kernel is built as Thumb,
            // userboot's entry point will be too, and Thumbify it.
#ifdef __thumb__
            *entry |= 1;
#endif

            // Map the vDSO right after it.
            *vdso_base = vmar->vmar()->base() + RoDso::size();
            status = vdso_->Map(mxtl::move(vmar), RoDso::size());
        }
        return status;
    }

private:
    const VDso* vdso_;
};

}; // anonymous namespace


// Get a handle to a VM object, with full rights except perhaps for writing.
static mx_status_t get_vmo_handle(mxtl::RefPtr<VmObject> vmo, bool readonly,
                                  mxtl::RefPtr<VmObjectDispatcher>* disp_ptr,
                                  Handle** ptr) {
    if (!vmo)
        return ERR_NO_MEMORY;
    mx_rights_t rights;
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_status_t result = VmObjectDispatcher::Create(
        mxtl::move(vmo), &dispatcher, &rights);
    if (result == NO_ERROR) {
        if (disp_ptr)
            *disp_ptr = mxtl::RefPtr<VmObjectDispatcher>::Downcast(dispatcher);
        if (readonly)
            rights &= ~MX_RIGHT_WRITE;
        if (ptr)
            *ptr = MakeHandle(mxtl::move(dispatcher), rights);
    }
    return result;
}

static mx_status_t get_job_handle(Handle** ptr) {
    mx_rights_t rights;
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_status_t result = JobDispatcher::Create(
        0u, GetRootJobDispatcher(), &dispatcher, &rights);
    if (result == NO_ERROR)
        *ptr = MakeHandle(mxtl::move(dispatcher), rights);
    return result;
}

static mx_status_t get_resource_handle(Handle** ptr) {
    mx_rights_t rights;
    mxtl::RefPtr<ResourceDispatcher> root;
    mx_status_t result = ResourceDispatcher::Create(&root, &rights, "root", MX_RREC_SELF_ROOT);
    root->MakeRoot();
    if (result == NO_ERROR)
        *ptr = MakeHandle(mxtl::RefPtr<Dispatcher>(root.get()), rights);
    return result;
}

// Create a channel and write the bootstrap message down one side of
// it, returning the handle to the other side.
static mx_handle_t make_bootstrap_channel(
    mxtl::RefPtr<ProcessDispatcher> process,
    mxtl::unique_ptr<MessagePacket> msg) {
    HandleOwner user_channel_handle;
    mxtl::RefPtr<ChannelDispatcher> kernel_channel;
    {
        mxtl::RefPtr<Dispatcher> mpd0, mpd1;
        mx_rights_t rights;
        status_t status = ChannelDispatcher::Create(0, &mpd0, &mpd1, &rights);
        if (status != NO_ERROR)
            return status;
        user_channel_handle.reset(MakeHandle(mxtl::move(mpd0), rights));
        kernel_channel = DownCastDispatcher<ChannelDispatcher>(&mpd1);
    }

    // Here it goes!
    mx_status_t status = kernel_channel->Write(mxtl::move(msg));
    if (status != NO_ERROR)
        return status;

    mx_handle_t hv = process->MapHandleToValue(user_channel_handle);
    process->AddHandle(mxtl::move(user_channel_handle));

    return hv;
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
    BOOTSTRAP_HANDLES
};

struct bootstrap_message {
    mx_proc_args_t header;
    uint32_t handle_info[BOOTSTRAP_HANDLES];
    char cmdline[CMDLINE_MAX];
};

static mxtl::unique_ptr<MessagePacket> prepare_bootstrap_message() {
    mxtl::unique_ptr<MessagePacket> packet;
    uint32_t data_size =
        static_cast<uint32_t>(offsetof(struct bootstrap_message, cmdline)) +
        __kernel_cmdline_size;
    uint32_t num_handles = BOOTSTRAP_HANDLES;
    if (MessagePacket::Create(data_size, num_handles, &packet) != NO_ERROR)
        return nullptr;

    bootstrap_message* msg =
        reinterpret_cast<bootstrap_message*>(packet->mutable_data());
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
        case BOOTSTRAP_HANDLES:
            __builtin_unreachable();
        }
        msg->handle_info[i] = info;
    }
    memcpy(msg->cmdline, __kernel_cmdline, __kernel_cmdline_size);

    return packet;
}

static int attempt_userboot() {
    size_t rsize;
    void* rbase = platform_get_ramdisk(&rsize);
    if (rbase)
        dprintf(INFO, "userboot: ramdisk %#15zx @ %p\n", rsize, rbase);

    auto stack_vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, stack_size);
    auto rootfs_vmo = VmObjectPaged::CreateFromROData(rbase, rsize);

    // Prepare the bootstrap message packet.  This puts its data (the
    // kernel command line) in place, and allocates space for its handles.
    // We'll fill in the handles as we create things.
    mxtl::unique_ptr<MessagePacket> msg = prepare_bootstrap_message();
    if (!msg)
        return ERR_NO_MEMORY;

    Handle** const handles = msg->mutable_handles();
    DEBUG_ASSERT(msg->num_handles() == BOOTSTRAP_HANDLES);
    mx_status_t status = get_vmo_handle(rootfs_vmo, false, nullptr,
                                        &handles[BOOTSTRAP_RAMDISK]);
    mxtl::RefPtr<VmObjectDispatcher> stack_vmo_dispatcher;
    if (status == NO_ERROR)
        status = get_vmo_handle(stack_vmo, false, &stack_vmo_dispatcher,
                                &handles[BOOTSTRAP_STACK]);
    if (status == NO_ERROR)
        status = get_resource_handle(&handles[BOOTSTRAP_RESOURCE_ROOT]);

    if (status == NO_ERROR)
        status = get_job_handle(&handles[BOOTSTRAP_JOB]);

    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<Dispatcher> proc_disp;
    mxtl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_rights_t rights, vmar_rights;
    status = ProcessDispatcher::Create(GetRootJobDispatcher(), "userboot", 0,
                                       &proc_disp, &rights,
                                       &vmar, &vmar_rights);
    if (status < 0)
        return status;

    handles[BOOTSTRAP_PROC] = MakeHandle(proc_disp, rights);

    auto proc = DownCastDispatcher<ProcessDispatcher>(&proc_disp);
    ASSERT(proc);

    handles[BOOTSTRAP_VMAR_ROOT] = MakeHandle(vmar, vmar_rights);

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
    if (status != NO_ERROR)
        return status;

    // Map the stack anywhere.
    mxtl::RefPtr<VmMapping> stack_mapping;
    status = vmar->Map(0,
                       mxtl::move(stack_vmo), 0, stack_size,
                       MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                       &stack_mapping);
    if (status != NO_ERROR)
        return status;

    uintptr_t stack_base = stack_mapping->base();
    uintptr_t sp = compute_initial_stack_pointer(stack_base, stack_size);

    // Create the user thread and stash its handle for the bootstrap message.
    mxtl::RefPtr<ThreadDispatcher> thread;
    {
        mxtl::RefPtr<Dispatcher> ut_disp;
        status = proc->CreateUserThread("userboot", 0, &ut_disp, &rights);
        if (status < 0)
            return status;
        handles[BOOTSTRAP_THREAD] = MakeHandle(ut_disp, rights);
        thread = DownCastDispatcher<ThreadDispatcher>(&ut_disp);
    }
    DEBUG_ASSERT(thread);

    // All the handles are in place, so we can send the bootstrap message.
    mx_handle_t hv = make_bootstrap_channel(proc, mxtl::move(msg));
    if (hv < 0)
        return hv;

    dprintf(SPEW, "userboot: %-23s @ %#" PRIxPTR "\n", "entry point", entry);

    // Start the process's initial thread.
    status = thread->Start(entry, sp, hv, vdso_base,
                           /* initial_thread= */ true);
    if (status != NO_ERROR) {
        printf("userboot: failed to start initial thread: %d\n", status);
        return status;
    }

    return NO_ERROR;
}

void userboot_init(uint level) {
#if !WITH_APP_SHELL
    dprintf(INFO, "userboot: console init\n");
    console_init();
#endif

    attempt_userboot();
}

LK_INIT_HOOK(userboot, userboot_init, LK_INIT_LEVEL_APPS - 1);
