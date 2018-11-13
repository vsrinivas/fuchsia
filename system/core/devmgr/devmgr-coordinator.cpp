// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <ddk/driver.h>
#include <driver-info/driver-info.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <launchpad/launchpad.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/wait.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/io.h>
#include <lib/fidl/coding.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/job.h>
#include <lib/zx/socket.h>
#include <zircon/assert.h>
#include <zircon/boot/bootdata.h>
#include <zircon/device/dmctl.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/system.h>

#include "devcoordinator.h"
#include "devhost.h"
#include "devmgr.h"
#include "fidl_txn.h"
#include "log.h"
#include "fshost.h"

namespace devmgr {

static void dc_driver_added(Driver* drv, const char* version);
static void dc_driver_added_init(Driver* drv, const char* version);


#define BOOT_FIRMWARE_DIR "/boot/lib/firmware"
#define SYSTEM_FIRMWARE_DIR "/system/lib/firmware"

extern zx_handle_t virtcon_open;

uint32_t log_flags = LOG_ERROR | LOG_INFO;

bool dc_asan_drivers = false;
bool dc_launched_first_devhost = false;

static zx::vmo bootdata_vmo;

static void dc_dump_state();
static void dc_dump_devprops();
static void dc_dump_drivers();

// Access the devcoordinator's async event loop
async::Loop* DcAsyncLoop() {
    // The constructor of this asserts that the loop allocation succeeds.  This
    // is fine, since if we can't successfully heap alloc during process
    // startup, the devcoordinator is not going to make it very far.
    static async::Loop loop(&kAsyncLoopConfigAttachToThread);
    return &loop;
}
static SuspendContext suspend_ctx;

static fbl::DoublyLinkedList<fbl::unique_ptr<Metadata>, Metadata::Node> published_metadata;

static bool dc_in_suspend() {
    return suspend_ctx.flags() == SuspendContext::Flags::kSuspend;
}
static void dc_suspend(uint32_t flags);
static void dc_mexec(zx::vmo kernel, zx::vmo bootdata);
static void dc_continue_suspend(SuspendContext* ctx);

static bool suspend_fallback = false;
static bool suspend_debug = false;

static Device root_device;
static Device misc_device;
static Device sys_device;
static Device test_device;

static zx_status_t initialize_core_devices() {
    {
        root_device.flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;
        root_device.protocol_id = ZX_PROTOCOL_ROOT;
        root_device.name = "root";
        root_device.libname = "";

        constexpr const char kArgs[] = "root,";
        auto args = fbl::make_unique<char[]>(sizeof(kArgs));
        if (!args) {
            return ZX_ERR_NO_MEMORY;
        }
        memcpy(args.get(), kArgs, sizeof(kArgs));
        root_device.args.reset(args.release());

        root_device.AddRef();
    }

    {
        misc_device.parent = &root_device;
        misc_device.flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;
        misc_device.protocol_id = ZX_PROTOCOL_MISC_PARENT;
        misc_device.name = "misc";
        misc_device.libname = "";

        constexpr const char kArgs[] = "misc,";
        auto args = fbl::make_unique<char[]>(sizeof(kArgs));
        if (!args) {
            return ZX_ERR_NO_MEMORY;
        }
        memcpy(args.get(), kArgs, sizeof(kArgs));
        misc_device.args.reset(args.release());

        misc_device.AddRef();
    }

    {
        sys_device.parent = &root_device;
        sys_device.flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE;
        sys_device.name = "sys";
        sys_device.libname = "";

        constexpr const char kArgs[] = "sys,";
        auto args = fbl::make_unique<char[]>(sizeof(kArgs));
        if (!args) {
            return ZX_ERR_NO_MEMORY;
        }
        memcpy(args.get(), kArgs, sizeof(kArgs));
        sys_device.args.reset(args.release());

        sys_device.AddRef();
    }

    {
        test_device.parent = &root_device;
        test_device.flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;
        test_device.protocol_id = ZX_PROTOCOL_TEST_PARENT;
        test_device.name = "test";
        test_device.libname = "";

        constexpr const char kArgs[] = "test,";
        auto args = fbl::make_unique<char[]>(sizeof(kArgs));
        if (!args) {
            return ZX_ERR_NO_MEMORY;
        }
        memcpy(args.get(), kArgs, sizeof(kArgs));
        test_device.args.reset(args.release());

        test_device.AddRef();
    }
    return ZX_OK;
}

static zx::socket dmctl_socket;

static void dmprintf(const char* fmt, ...) {
    if (!dmctl_socket.is_valid()) {
        return;
    }
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t actual;
    if (dmctl_socket.write(0, buf, strlen(buf), &actual) != ZX_OK) {
        dmctl_socket.reset();
    }
}

static zx_status_t handle_dmctl_write(size_t len, const char* cmd) {
    if (len == 4) {
        if (!memcmp(cmd, "dump", 4)) {
            dc_dump_state();
            return ZX_OK;
        }
        if (!memcmp(cmd, "help", 4)) {
            dmprintf("dump              - dump device tree\n"
                     "poweroff          - power off the system\n"
                     "shutdown          - power off the system\n"
                     "suspend           - suspend the system to RAM\n"
                     "reboot            - reboot the system\n"
                     "reboot-bootloader - reboot the system into boatloader\n"
                     "reboot-recovery   - reboot the system into recovery\n"
                     "kerneldebug       - send a command to the kernel\n"
                     "ktraceoff         - stop kernel tracing\n"
                     "ktraceon          - start kernel tracing\n"
                     "devprops          - dump published devices and their binding properties\n"
                     "drivers           - list discovered drivers and their properties\n"
                     );
            return ZX_OK;
        }
    }
    if ((len == 7) && !memcmp(cmd, "drivers", 7)) {
        dc_dump_drivers();
        return ZX_OK;
    }
    if (len == 8) {
        if (!memcmp(cmd, "ktraceon", 8)) {
            zx_ktrace_control(get_root_resource(), KTRACE_ACTION_START, KTRACE_GRP_ALL, nullptr);
            return ZX_OK;
        }
        if (!memcmp(cmd, "devprops", 8)) {
            dc_dump_devprops();
            return ZX_OK;
        }
    }
    if ((len == 9) && (!memcmp(cmd, "ktraceoff", 9))) {
        zx_ktrace_control(get_root_resource(), KTRACE_ACTION_STOP, 0, nullptr);
        zx_ktrace_control(get_root_resource(), KTRACE_ACTION_REWIND, 0, nullptr);
        return ZX_OK;
    }
    if ((len > 12) && !memcmp(cmd, "kerneldebug ", 12)) {
        return zx_debug_send_command(get_root_resource(), cmd + 12, len - 12);
    }

    if (dc_in_suspend()) {
        log(ERROR, "devcoord: rpc: dm-command \"%.*s\" forbidden in suspend\n",
            static_cast<uint32_t>(len), cmd);
        return ZX_ERR_BAD_STATE;
    }

    if ((len == 6) && !memcmp(cmd, "reboot", 6)) {
        devmgr_vfs_exit();
        dc_suspend(DEVICE_SUSPEND_FLAG_REBOOT);
        return ZX_OK;
    }
    if ((len == 17) && !memcmp(cmd, "reboot-bootloader", 17)) {
        devmgr_vfs_exit();
        dc_suspend(DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER);
        return ZX_OK;
    }
    if ((len == 15) && !memcmp(cmd, "reboot-recovery", 15)) {
        devmgr_vfs_exit();
        dc_suspend(DEVICE_SUSPEND_FLAG_REBOOT_RECOVERY);
        return ZX_OK;
    }
    if ((len == 7) && !memcmp(cmd, "suspend", 7)) {
        dc_suspend(DEVICE_SUSPEND_FLAG_SUSPEND_RAM);
        return ZX_OK;
    }
    if (len == 8 && (!memcmp(cmd, "poweroff", 8) || !memcmp(cmd, "shutdown", 8))) {
        devmgr_vfs_exit();
        dc_suspend(DEVICE_SUSPEND_FLAG_POWEROFF);
        return ZX_OK;
    }
    if ((len > 11) && !memcmp(cmd, "add-driver:", 11)) {
        len -= 11;
        char path[len + 1];
        memcpy(path, cmd + 11, len);
        path[len] = 0;
        load_driver(path, dc_driver_added);
        return ZX_OK;
    }
    dmprintf("unknown command\n");
    log(ERROR, "dmctl: unknown command '%.*s'\n", (int) len, cmd);
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t dc_attempt_bind(const Driver* drv, Device* dev);

static bool dc_running;

static zx::channel dc_watch_channel;

static zx::job devhost_job;

// All Drivers
static fbl::DoublyLinkedList<Driver*, Driver::Node> list_drivers;

// Drivers to add to All Drivers
static fbl::DoublyLinkedList<Driver*, Driver::Node> list_drivers_new;

// Drivers to try last
static fbl::DoublyLinkedList<Driver*, Driver::Node> list_drivers_fallback;

// All Devices (excluding static immortal devices)
static fbl::DoublyLinkedList<Device*, Device::AllDevicesNode> list_devices;

// All DevHosts
static fbl::DoublyLinkedList<Devhost*, Devhost::AllDevhostsNode> list_devhosts;

static const Driver* libname_to_driver(const char* libname) {
    for (const auto& drv : list_drivers) {
        if (!strcmp(libname, drv.libname.c_str())) {
            return &drv;
        }
    }
    return nullptr;
}

static zx_status_t load_vmo(const char* libname, zx::vmo* out_vmo) {
    int fd = open(libname, O_RDONLY);
    if (fd < 0) {
        log(ERROR, "devcoord: cannot open driver '%s'\n", libname);
        return ZX_ERR_IO;
    }
    zx::vmo vmo;
    zx_status_t r = fdio_get_vmo_clone(fd, vmo.reset_and_get_address());
    close(fd);
    if (r < 0) {
        log(ERROR, "devcoord: cannot get driver vmo '%s'\n", libname);
    }
    const char* vmo_name = strrchr(libname, '/');
    if (vmo_name != nullptr) {
        ++vmo_name;
    } else {
        vmo_name = libname;
    }
    vmo.set_property(ZX_PROP_NAME, vmo_name, strlen(vmo_name));
    *out_vmo = fbl::move(vmo);
    return r;
}

static zx_status_t libname_to_vmo(const char* libname, zx::vmo* out_vmo) {
    const Driver* drv = libname_to_driver(libname);
    if (drv == nullptr) {
        log(ERROR, "devcoord: cannot find driver '%s'\n", libname);
        return ZX_ERR_NOT_FOUND;
    }

    // Check for cached DSO
    if (drv->dso_vmo != ZX_HANDLE_INVALID) {
        zx_status_t r = drv->dso_vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHTS_PROPERTY |
                                               ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP,
                                               out_vmo);
        if (r != ZX_OK) {
            log(ERROR, "devcoord: cannot duplicate cached dso for '%s' '%s'\n", drv->name.c_str(),
                libname);
        }
        return r;
    } else {
        return load_vmo(libname, out_vmo);
    }
}

void devmgr_set_bootdata(const zx::unowned_vmo vmo) {
    if (bootdata_vmo.is_valid()) {
        return;
    }
    vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &bootdata_vmo);
}

static void dc_dump_device(const Device* dev, size_t indent) {
    zx_koid_t pid = dev->host ? dev->host->koid : 0;
    char extra[256];
    if (log_flags & LOG_DEVLC) {
        snprintf(extra, sizeof(extra), " dev=%p ref=%d", dev, dev->refcount_);
    } else {
        extra[0] = 0;
    }
    if (pid == 0) {
        dmprintf("%*s[%s]%s\n", (int) (indent * 3), "", dev->name, extra);
    } else {
        dmprintf("%*s%c%s%c pid=%zu%s %s\n",
                 (int) (indent * 3), "",
                 dev->flags & DEV_CTX_PROXY ? '<' : '[',
                 dev->name,
                 dev->flags & DEV_CTX_PROXY ? '>' : ']',
                 pid, extra,
                 dev->libname ? dev->libname : "");
    }
    if (dev->proxy) {
        indent++;
        dc_dump_device(dev->proxy, indent);
    }
    for (const auto& child : dev->children) {
        dc_dump_device(&child, indent + 1);
    }
}

static void dc_dump_state() {
    dc_dump_device(&root_device, 0);
    dc_dump_device(&misc_device, 1);
    dc_dump_device(&sys_device, 1);
    dc_dump_device(&test_device, 1);
}

static void dc_dump_device_props(const Device* dev) {
    if (dev->host) {
        dmprintf("Name [%s]%s%s%s\n",
                 dev->name,
                 dev->libname ? " Driver [" : "",
                 dev->libname ? dev->libname : "",
                 dev->libname ? "]" : "");
        dmprintf("Flags   :%s%s%s%s%s%s%s\n",
                 dev->flags & DEV_CTX_IMMORTAL     ? " Immortal"  : "",
                 dev->flags & DEV_CTX_MUST_ISOLATE ? " Isolate"   : "",
                 dev->flags & DEV_CTX_MULTI_BIND   ? " MultiBind" : "",
                 dev->flags & DEV_CTX_BOUND        ? " Bound"     : "",
                 dev->flags & DEV_CTX_DEAD         ? " Dead"      : "",
                 dev->flags & DEV_CTX_ZOMBIE       ? " Zombie"    : "",
                 dev->flags & DEV_CTX_PROXY        ? " Proxy"     : "");

        char a = (char)((dev->protocol_id >> 24) & 0xFF);
        char b = (char)((dev->protocol_id >> 16) & 0xFF);
        char c = (char)((dev->protocol_id >> 8) & 0xFF);
        char d = (char)(dev->protocol_id & 0xFF);
        dmprintf("ProtoId : '%c%c%c%c' 0x%08x(%u)\n",
                 isprint(a) ? a : '.',
                 isprint(b) ? b : '.',
                 isprint(c) ? c : '.',
                 isprint(d) ? d : '.',
                 dev->protocol_id,
                 dev->protocol_id);

        dmprintf("%u Propert%s\n", dev->prop_count, dev->prop_count == 1 ? "y" : "ies");
        for (uint32_t i = 0; i < dev->prop_count; ++i) {
            const zx_device_prop_t* p = &dev->props[i];
            const char* param_name = di_bind_param_name(p->id);

            if (param_name) {
                dmprintf("[%2u/%2u] : Value 0x%08x Id %s\n",
                         i, dev->prop_count, p->value, param_name);
            } else {
                dmprintf("[%2u/%2u] : Value 0x%08x Id 0x%04hx\n",
                         i, dev->prop_count, p->value, p->id);
            }
        }
        dmprintf("\n");
    }

    if (dev->proxy) {
        dc_dump_device_props(dev->proxy);
    }
    for (const auto& child : dev->children) {
        dc_dump_device_props(&child);
    }
}

static void dc_dump_devprops() {
    dc_dump_device_props(&root_device);
    dc_dump_device_props(&misc_device);
    dc_dump_device_props(&sys_device);
    dc_dump_device_props(&test_device);
}

static void dc_dump_drivers() {
    bool first = true;
    for (const auto& drv : list_drivers) {
        dmprintf("%sName    : %s\n", first ? "" : "\n", drv.name.c_str());
        dmprintf("Driver  : %s\n", !drv.libname.empty() ? drv.libname.c_str() : "(null)");
        dmprintf("Flags   : 0x%08x\n", drv.flags);
        if (drv.binding_size) {
            char line[256];
            uint32_t count = drv.binding_size / static_cast<uint32_t>(sizeof(drv.binding[0]));
            dmprintf("Binding : %u instruction%s (%u bytes)\n",
                     count, (count == 1) ? "" : "s", drv.binding_size);
            for (uint32_t i = 0; i < count; ++i) {
                di_dump_bind_inst(&drv.binding[i], line, sizeof(line));
                dmprintf("[%u/%u]: %s\n", i + 1, count, line);
            }
        }
        first = false;
    }
}

static void dc_handle_new_device(Device* dev);
static void dc_handle_new_driver();

static fbl::DoublyLinkedList<Work*, Work::Node> list_pending_work;

static void queue_work(Work* work, Work::Op op, uint32_t arg) {
    ZX_ASSERT(work->op == Work::Op::kIdle);
    work->op = op;
    work->arg = arg;
    list_pending_work.push_back(work);
}

static void cancel_work(Work* work) {
    if (work->op != Work::Op::kIdle) {
        list_pending_work.erase(*work);
        work->op = Work::Op::kIdle;
    }
}

static void process_work(Work* work) {
    Work::Op op = work->op;
    work->op = Work::Op::kIdle;

    switch (op) {
    case Work::Op::kDeviceAdded: {
        Device* dev = work->owner;
        dc_handle_new_device(dev);
        break;
    }
    case Work::Op::kDriverAdded: {
        dc_handle_new_driver();
        break;
    }
    default:
        log(ERROR, "devcoord: unknown work: op=%u\n", static_cast<uint32_t>(op));
    }
}

static const char* get_devhost_bin() {
    // If there are any ASan drivers, use the ASan-supporting devhost for
    // all drivers because even a devhost launched initially with just a
    // non-ASan driver might later load an ASan driver.  One day we might
    // be able to be more flexible about which drivers must get loaded into
    // the same devhost and thus be able to use both ASan and non-ASan
    // devhosts at the same time when only a subset of drivers use ASan.
    if (dc_asan_drivers)
        return "/boot/bin/devhost.asan";
    return "/boot/bin/devhost";
}

zx_handle_t get_service_root();

static zx_status_t dc_get_topo_path(const Device* dev, char* out, size_t max) {
    char tmp[max];
    char* path = tmp + max - 1;
    *path = 0;
    size_t total = 1;

    while (dev != nullptr) {
        if (dev->flags & DEV_CTX_PROXY) {
            dev = dev->parent;
        }
        const char* name;

        if (dev->parent) {
            name = dev->name;
        } else if (!strcmp(misc_device.name, dev->name)) {
            name = "dev/misc";
        } else if (!strcmp(sys_device.name, dev->name)) {
            name = "dev/sys";
        } else if (!strcmp(sys_device.name, dev->name)) {
            name = "dev/test";
        } else {
            name = "dev";
        }
        size_t len = strlen(name) + 1;
        if (len > (max - total)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(path - len + 1, name, len - 1);
        path -= len;
        *path = '/';
        total += len;
        dev = dev->parent;
    }

    memcpy(out, path, total);
    return ZX_OK;
}

//TODO: use a better device identifier
static zx_status_t dc_notify(const Device* dev, uint32_t op) {
    if (!dc_watch_channel.is_valid()) {
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t r;
    if (op == DEVMGR_OP_DEVICE_ADDED) {
        size_t propslen = sizeof(zx_device_prop_t) * dev->prop_count;
        size_t len = sizeof(devmgr_event_t) + propslen;
        char msg[len + fuchsia_device_manager_PATH_MAX];
        auto evt = reinterpret_cast<devmgr_event_t*>(msg);
        memset(evt, 0, sizeof(devmgr_event_t));
        memcpy(msg + sizeof(devmgr_event_t), dev->props.get(), propslen);
        if (dc_get_topo_path(dev, msg + len, fuchsia_device_manager_PATH_MAX) < 0) {
            return ZX_OK;
        }
        size_t pathlen = strlen(msg + len);
        len += pathlen;
        evt->opcode = op;
        if (dev->flags & DEV_CTX_BOUND) {
            evt->flags |= DEVMGR_FLAGS_BOUND;
        }
        evt->id = (uintptr_t) dev;
        evt->u.add.protocol_id = dev->protocol_id;
        evt->u.add.props_len = static_cast<uint32_t>(propslen);
        evt->u.add.path_len = static_cast<uint32_t>(pathlen);
        r = dc_watch_channel.write(0, msg, static_cast<uint32_t>(len), nullptr, 0);
    } else {
        devmgr_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.opcode = op;
        if (dev->flags & DEV_CTX_BOUND) {
            evt.flags |= DEVMGR_FLAGS_BOUND;
        }
        evt.id = (uintptr_t) dev;
        r = dc_watch_channel.write(0, &evt, sizeof(evt), nullptr, 0);
    }
    if (r != ZX_OK) {
        dc_watch_channel.reset();
    }
    return r;
}

static void dc_watch(zx::channel h) {
    dc_watch_channel = fbl::move(h);
    for (const auto& dev : list_devices) {
        if (dev.flags & (DEV_CTX_DEAD | DEV_CTX_ZOMBIE)) {
            // if device is dead, ignore it
            continue;
        }
        if (dc_notify(&dev, DEVMGR_OP_DEVICE_ADDED) < 0) {
            break;
        }
    }
}

static zx_status_t dc_launch_devhost(Devhost* host,
                                     const char* name, zx_handle_t hrpc) {
    const char* devhost_bin = get_devhost_bin();

    launchpad_t* lp;
    launchpad_create_with_jobs(devhost_job.get(), 0, name, &lp);
    launchpad_load_from_file(lp, devhost_bin);
    launchpad_set_args(lp, 1, &devhost_bin);

    launchpad_add_handle(lp, hrpc, PA_HND(PA_USER0, 0));

    zx_handle_t h;
    //TODO: limit root resource to root devhost only
    zx_handle_duplicate(get_root_resource(), ZX_RIGHT_SAME_RIGHTS, &h);
    launchpad_add_handle(lp, h, PA_HND(PA_RESOURCE, 0));

    // Inherit devmgr's environment (including kernel cmdline)
    launchpad_clone(lp, LP_CLONE_ENVIRON);

    const char* nametable[2] = { "/boot", "/svc", };
    uint32_t name_count = 0;

    //TODO: eventually devhosts should not have vfs access
    launchpad_add_handle(lp, fs_clone("boot").release(),
                         PA_HND(PA_NS_DIR, name_count++));

    //TODO: constrain to /svc/device
    if ((h = fs_clone("svc").release()) != ZX_HANDLE_INVALID) {
        launchpad_add_handle(lp, h, PA_HND(PA_NS_DIR, name_count++));
    }

    launchpad_set_nametable(lp, name_count, nametable);

    //TODO: limit root job access to root devhost only
    launchpad_add_handle(lp, get_sysinfo_job_root().release(),
                         PA_HND(PA_USER0, ID_HJOBROOT));

    const char* errmsg;
    zx_status_t status = launchpad_go(lp, host->proc.reset_and_get_address(), &errmsg);
    if (status < 0) {
        log(ERROR, "devcoord: launch devhost '%s': failed: %d: %s\n",
            name, status, errmsg);
        return status;
    }
    zx_info_handle_basic_t info;
    if (host->proc.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) == ZX_OK) {
        host->koid = info.koid;
    }
    log(INFO, "devcoord: launch devhost '%s': pid=%zu\n",
        name, host->koid);

    dc_launched_first_devhost = true;

    return ZX_OK;
}

Devhost::Devhost()
    : hrpc(ZX_HANDLE_INVALID), proc(ZX_HANDLE_INVALID), koid(0), refcount_(0),
      flags(0), parent(nullptr), anode({}), snode({}), node({}) {
}

static zx_status_t dc_new_devhost(const char* name, Devhost* parent,
                                  Devhost** out) {
    auto dh = fbl::make_unique<Devhost>();
    if (dh == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_handle_t hrpc;
    zx_status_t r;
    if ((r = zx_channel_create(0, &hrpc, &dh->hrpc)) < 0) {
        return r;
    }

    if ((r = dc_launch_devhost(dh.get(), name, hrpc)) < 0) {
        zx_handle_close(dh->hrpc);
        return r;
    }

    if (parent) {
        dh->parent = parent;
        dh->parent->AddRef();
        dh->parent->children.push_back(dh.get());
    }
    list_devhosts.push_back(dh.get());

    log(DEVLC, "devcoord: new host %p\n", dh.get());

    *out = dh.release();
    return ZX_OK;
}

static void dc_release_devhost(Devhost* dh) {
    if (!dh->Release()) {
        return;
    }
    log(INFO, "devcoord: destroy host %p\n", dh);
    Devhost* parent = dh->parent;
    if (parent != nullptr) {
        dh->parent->children.erase(*dh);
        dh->parent = nullptr;
        dc_release_devhost(parent);
    }
    list_devhosts.erase(*dh);
    zx_handle_close(dh->hrpc);
    dh->proc.kill();
    delete dh;
}

// called when device children or proxys are removed
static void dc_release_device(Device* dev) {
    log(DEVLC, "devcoord: release dev %p name='%s' ref=%d\n", dev, dev->name, dev->refcount_);

    if (!dev->Release()) {
        return;
    }

    // Immortal devices are never destroyed
    if (dev->flags & DEV_CTX_IMMORTAL) {
        return;
    }

    log(DEVLC, "devcoord: destroy dev %p name='%s'\n", dev, dev->name);

    devfs_unpublish(dev);

    if (dev->hrpc.is_valid()) {
        dev->wait.set_object(ZX_HANDLE_INVALID);
        dev->hrpc.reset();
    }
    dev->host = nullptr;

    cancel_work(&dev->work);

    fbl::unique_ptr<Metadata> md;
    while ((md = dev->metadata.pop_front()) != nullptr) {
        if (md->has_path) {
            // return to published_metadata list
            published_metadata.push_back(fbl::move(md));
        } else {
            // metadata was attached directly to this device, so we release it now
        }
    }

    //TODO: cancel any pending rpc responses
    //TODO: Have dtor assert that DEV_CTX_IMMORTAL set on flags
    delete dev;
}

Device::Device()
    : flags(0), host(nullptr),
      name(nullptr), libname(nullptr), work(this), refcount_(0), protocol_id(0), prop_count(0),
      self(nullptr), link(nullptr), parent(nullptr), proxy(nullptr), node({}), dhnode({}),
      anode({}) {
}

Device::~Device() {
    // TODO: cancel any pending rpc responses.  This clear is a hack to prevent
    // pending's dtor from asserting.
    pending.clear();
}

// Add a new device to a parent device (same devhost)
// New device is published in devfs.
// Caller closes handles on error, so we don't have to.
static zx_status_t dc_add_device(Device* parent, zx::channel rpc,
                                 const uint64_t* props_data, size_t props_count,
                                 fbl::StringPiece name,
                                 uint32_t protocol_id,
                                 fbl::StringPiece driver_path,
                                 fbl::StringPiece args,
                                 bool invisible) {
    // If this is true, then |name_data|'s size is properly bounded.
    static_assert(fuchsia_device_manager_DEVICE_NAME_MAX == ZX_DEVICE_NAME_MAX);
    static_assert(fuchsia_device_manager_PROPERTIES_MAX <= UINT32_MAX);

    if (dc_in_suspend()) {
        log(ERROR, "devcoord: rpc: add-device '%.*s' forbidden in suspend\n",
            static_cast<int>(name.size()), name.data());
        return ZX_ERR_BAD_STATE;
    }

    log(RPC_IN, "devcoord: rpc: add-device '%.*s' args='%.*s'\n",
        static_cast<int>(name.size()), name.data(), static_cast<int>(args.size()), args.data());

    auto dev = fbl::make_unique<Device>();
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }

    auto args_buf = fbl::make_unique<char[]>(args.size() + 1);
    dev->props = fbl::make_unique<zx_device_prop_t[]>(props_count);
    dev->name_alloc_ = fbl::make_unique<char[]>(driver_path.size() + name.size() + 2);
    if (!args_buf || !dev->props || !dev->name_alloc_) {
        return ZX_ERR_NO_MEMORY;
    }

    dev->hrpc = fbl::move(rpc);
    dev->prop_count = static_cast<uint32_t>(props_count);
    dev->protocol_id = protocol_id;

    memcpy(args_buf.get(), args.data(), args.size());
    args_buf[args.size()] = 0;
    // release+reset is used here to add const to the inner type
    dev->args.reset(args_buf.release());

    memcpy(dev->name_alloc_.get(), name.data(), name.size());
    dev->name_alloc_[name.size()] = 0;
    dev->name = dev->name_alloc_.get();

    char* libname_buf = &dev->name_alloc_[name.size()+1];
    memcpy(libname_buf, driver_path.data(), driver_path.size());
    libname_buf[driver_path.size()] = 0;
    dev->libname = libname_buf;

    static_assert(sizeof(zx_device_prop_t) == sizeof(props_data[0]));
    memcpy(dev->props.get(), props_data, props_count * sizeof(zx_device_prop_t));

    // If we have bus device args we are, by definition, a bus device.
    if (args.size() > 0) {
        dev->flags |= DEV_CTX_MUST_ISOLATE;
    }

    // We exist within our parent's device host
    dev->host = parent->host;

    // If our parent is a proxy, for the purpose
    // of devicefs, we need to work with *its* parent
    // which is the device that it is proxying.
    if (parent->flags & DEV_CTX_PROXY) {
        parent = parent->parent;
    }
    dev->parent = parent;

    // We must mark the device as invisible before publishing so
    // that we don't send "device added" notifications.
    if (invisible) {
        dev->flags |= DEV_CTX_INVISIBLE;
    }

    zx_status_t r;
    if ((r = devfs_publish(parent, dev.get())) < 0) {
        return r;
    }

    dev->wait.set_object(dev->hrpc.get());
    dev->wait.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    if ((r = dev->wait.Begin(DcAsyncLoop()->dispatcher())) != ZX_OK) {
        devfs_unpublish(dev.get());
        return r;
    }

    if (dev->host) {
        //TODO host == nullptr should be impossible
        dev->host->AddRef();
        dev->host->devices.push_back(dev.get());
    }
    dev->AddRef();
    parent->children.push_back(dev.get());
    parent->AddRef();

    list_devices.push_back(dev.get());

    log(DEVLC, "devcoord: dev %p name='%s' ++ref=%d (child)\n",
        parent, parent->name, parent->refcount_);

    log(DEVLC, "devcoord: publish %p '%s' props=%u args='%s' parent=%p\n",
        dev.get(), dev->name, dev->prop_count, dev->args.get(), dev->parent);

    if (!invisible) {
        dc_notify(dev.get(), DEVMGR_OP_DEVICE_ADDED);
        queue_work(&dev->work, Work::Op::kDeviceAdded, 0);
    }
    // TODO(teisenbe/kulakowski): This should go away once we switch to refptrs
    // here
    __UNUSED auto ptr = dev.release();
    return ZX_OK;
}

static zx_status_t dc_make_visible(Device* dev) {
    if (dev->flags & DEV_CTX_DEAD) {
        return ZX_ERR_BAD_STATE;
    }
    if (dev->flags & DEV_CTX_INVISIBLE) {
        dev->flags &= ~DEV_CTX_INVISIBLE;
        devfs_advertise(dev);
        dc_notify(dev, DEVMGR_OP_DEVICE_ADDED);
        queue_work(&dev->work, Work::Op::kDeviceAdded, 0);
    }
    return ZX_OK;
}

// Remove device from parent
// forced indicates this is removal due to a channel close
// or process exit, which means we should remove all other
// devices that share the devhost at the same time
static zx_status_t dc_remove_device(Device* dev, bool forced) {
    if (dev->flags & DEV_CTX_ZOMBIE) {
        // This device was removed due to its devhost dying
        // (process exit or some other channel on that devhost
        // closing), and is now receiving the final remove call
        dev->flags &= (~DEV_CTX_ZOMBIE);
        dc_release_device(dev);
        return ZX_OK;
    }
    if (dev->flags & DEV_CTX_DEAD) {
        // This should not happen
        log(ERROR, "devcoord: cannot remove dev %p name='%s' twice!\n", dev, dev->name);
        return ZX_ERR_BAD_STATE;
    }
    if (dev->flags & DEV_CTX_IMMORTAL) {
        // This too should not happen
        log(ERROR, "devcoord: cannot remove dev %p name='%s' (immortal)\n", dev, dev->name);
        return ZX_ERR_BAD_STATE;
    }

    log(DEVLC, "devcoord: remove %p name='%s' parent=%p\n", dev, dev->name, dev->parent);
    dev->flags |= DEV_CTX_DEAD;

    // remove from devfs, preventing further OPEN attempts
    devfs_unpublish(dev);

    if (dev->proxy) {
        zx_status_t r = dh_send_remove_device(dev->proxy);
        if (r != ZX_OK) {
            log(ERROR, "devcoord: failed to send message in dc_remove_device: %d\n", r);
        }
    }

    // detach from devhost
    Devhost* dh = dev->host;
    if (dh != nullptr) {
        dev->host->devices.erase(*dev);
        dev->host = nullptr;

        // If we are responding to a disconnect,
        // we'll remove all the other devices on this devhost too.
        // A side-effect of this is that the devhost will be released,
        // as well as any proxy devices.
        if (forced) {
            dh->flags |= DEV_HOST_DYING;

            Device* next;
            Device* last = nullptr;
            while (!dh->devices.is_empty()) {
                next = &dh->devices.front();
                if (last == next) {
                    // This shouldn't be possbile, but let's not infinite-loop if it happens
                    log(ERROR, "devcoord: fatal: failed to remove dev %p from devhost\n", next);
                    exit(1);
                }
                dc_remove_device(next, false);
                last = next;
            }

            //TODO: set a timer so if this devhost does not finish dying
            //      in a reasonable amount of time, we fix the glitch.
        }

        dc_release_devhost(dh);
    }

    // if we have a parent, disconnect and downref it
    Device* parent = dev->parent;
    if (parent != nullptr) {
        dev->parent = nullptr;
        if (dev->flags & DEV_CTX_PROXY) {
            parent->proxy = nullptr;
        } else {
            parent->children.erase(*dev);
            if (parent->children.is_empty()) {
                parent->flags &= (~DEV_CTX_BOUND);

                //TODO: This code is to cause the bind process to
                //      restart and get a new devhost to be launched
                //      when a devhost dies.  It should probably be
                //      more tied to devhost teardown than it is.

                // IF we are the last child of our parent
                // AND our parent is not itself dead
                // AND our parent is a BUSDEV
                // AND our parent's devhost is not dying
                // THEN we will want to rebind our parent
                if (!(parent->flags & DEV_CTX_DEAD) &&
                    (parent->flags & DEV_CTX_MUST_ISOLATE) &&
                    ((parent->host == nullptr) || !(parent->host->flags & DEV_HOST_DYING))) {

                    log(DEVLC, "devcoord: bus device %p name='%s' is unbound\n",
                        parent, parent->name);

                    //TODO: introduce timeout, exponential backoff
                    queue_work(&parent->work, Work::Op::kDeviceAdded, 0);
                }
            }
        }
        dc_release_device(parent);
    }

    if (!(dev->flags & DEV_CTX_PROXY)) {
        // remove from list of all devices
        list_devices.erase(*dev);
        dc_notify(dev, DEVMGR_OP_DEVICE_REMOVED);
    }

    if (forced) {
        // release the ref held by the devhost
        dc_release_device(dev);
    } else {
        // Mark the device as a zombie but don't drop the
        // (likely) final reference.  The caller needs to
        // finish replying to the RPC and dropping the
        // reference would close the RPC channel.
        dev->flags |= DEV_CTX_ZOMBIE;
    }
    return ZX_OK;
}

static zx_status_t dc_bind_device(Device* dev, fbl::StringPiece drvlibname) {
     log(INFO, "devcoord: dc_bind_device() '%.*s'\n", static_cast<int>(drvlibname.size()),
         drvlibname.data());

    // shouldn't be possible to get a bind request for a proxy device
    if (dev->flags & DEV_CTX_PROXY) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // A libname of "" means a general rebind request
    // instead of a specific request
    bool autobind = (drvlibname.size() == 0);

    //TODO: disallow if we're in the middle of enumeration, etc
    for (const auto& drv : list_drivers) {
        if (autobind || !drvlibname.compare(drv.libname)) {
            if (dc_is_bindable(&drv, dev->protocol_id,
                               dev->props.get(), dev->prop_count, autobind)) {
                log(SPEW, "devcoord: drv='%s' bindable to dev='%s'\n",
                    drv.name.c_str(), dev->name);
                dc_attempt_bind(&drv, dev);
                return ZX_OK;
            }
        }
    }

    // Notify observers that this device is available again
    // Needed for non-auto-binding drivers like GPT against block, etc
    if (autobind) {
        devfs_advertise_modified(dev);
    }

    return ZX_OK;
};

static zx_status_t dc_load_firmware(Device* dev, const char* path,
                                    zx::vmo* vmo, size_t* size) {
    static const char* fwdirs[] = {
        BOOT_FIRMWARE_DIR,
        SYSTEM_FIRMWARE_DIR,
    };

    // Must be a relative path and no funny business.
    if (path[0] == '/' || path[0] == '.') {
        return ZX_ERR_INVALID_ARGS;
    }

    int fd, fwfd;
    for (unsigned n = 0; n < fbl::count_of(fwdirs); n++) {
        if ((fd = open(fwdirs[n], O_RDONLY, O_DIRECTORY)) < 0) {
            continue;
        }
        fwfd = openat(fd, path, O_RDONLY);
        close(fd);
        if (fwfd >= 0) {
            *size = lseek(fwfd, 0, SEEK_END);
            zx_status_t r = fdio_get_vmo_clone(fwfd, vmo->reset_and_get_address());
            close(fwfd);
            return r;
        }
        if (errno != ENOENT) {
            return ZX_ERR_IO;
        }
    }
    return ZX_ERR_NOT_FOUND;
}

// Returns true if the parent path is equal to or specifies a child device of the parent.
static bool path_is_child(const char* parent_path, const char* child_path) {
    size_t parent_length = strlen(parent_path);
    return (!strncmp(parent_path, child_path, parent_length) &&
        (child_path[parent_length] == 0 || child_path[parent_length] == '/'));
}

static zx_status_t dc_get_metadata(Device* dev, uint32_t type, void* buffer, size_t buflen,
                                   size_t* actual) {
    // search dev and its parent devices for a match
    Device* test = dev;
    while (test) {
        for (const auto& md : test->metadata) {
            if (md.type == type) {
                if (md.length > buflen) {
                    return ZX_ERR_BUFFER_TOO_SMALL;
                }
                memcpy(buffer, md.Data(), md.length);
                *actual = md.length;
                return ZX_OK;
            }
        }
        test = test->parent;
    }

    // if no metadata is found, check list of metadata added via device_publish_metadata()
    char path[fuchsia_device_manager_PATH_MAX];
    zx_status_t status = dc_get_topo_path(dev, path, sizeof(path));
    if (status != ZX_OK) {
        return status;
    }

    for (const auto& md : published_metadata) {
        const char* md_path = md.Data() + md.length;
        if (md.type == type && path_is_child(md_path, path)) {
            if (md.length > buflen) {
                return ZX_ERR_BUFFER_TOO_SMALL;
            }
            memcpy(buffer, md.Data(), md.length);
            *actual = md.length;
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_FOUND;
}

static zx_status_t dc_add_metadata(Device* dev, uint32_t type, const void* data,
                                   uint32_t length) {
    fbl::unique_ptr<Metadata> md;
    zx_status_t status = Metadata::Create(length, &md);
    if (status != ZX_OK) {
        return status;
    }

    md->type = type;
    md->length = length;
    memcpy(md->Data(), data, length);
    dev->metadata.push_front(fbl::move(md));
    return ZX_OK;
}

static zx_status_t dc_publish_metadata(Device* dev, const char* path, uint32_t type,
                                       const void* data, uint32_t length) {
    char caller_path[fuchsia_device_manager_PATH_MAX];
    zx_status_t status = dc_get_topo_path(dev, caller_path, sizeof(caller_path));
    if (status != ZX_OK) {
        return status;
    }

    // Check to see if the specified path is a child of the caller's path
    if (path_is_child(caller_path, path)) {
        // Caller is adding a path that matches itself or one of its children, which is allowed.
    } else {
        // Adding metadata to arbitrary paths is restricted to drivers running in the sys devhost.
        while (dev && dev != &sys_device) {
            if (dev->proxy) {
                // this device is in a child devhost
                return ZX_ERR_ACCESS_DENIED;
            }
            dev = dev->parent;
        }
        if (!dev) {
            return ZX_ERR_ACCESS_DENIED;
        }
    }

    fbl::unique_ptr<Metadata> md;
    status = Metadata::Create(length + strlen(path) + 1, &md);
    if (status != ZX_OK) {
        return status;
    }

    md->type = type;
    md->length = length;
    md->has_path = true;
    memcpy(md->Data(), data, length);
    strcpy(md->Data() + length, path);
    published_metadata.push_front(fbl::move(md));
    return ZX_OK;
}

static zx_status_t fidl_AddDevice(void* ctx, zx_handle_t raw_rpc,
                                  const uint64_t* props_data, size_t props_count,
                                  const char* name_data, size_t name_size,
                                  uint32_t protocol_id,
                                  const char* driver_path_data, size_t driver_path_size,
                                  const char* args_data, size_t args_size, fidl_txn_t* txn) {
    auto parent = static_cast<Device*>(ctx);
    zx::channel rpc(raw_rpc);
    fbl::StringPiece name(name_data, name_size);
    fbl::StringPiece driver_path(driver_path_data, driver_path_size);
    fbl::StringPiece args(args_data, args_size);

    zx_status_t status = dc_add_device(parent, fbl::move(rpc), props_data, props_count,
                                       name, protocol_id, driver_path, args, false);
    return fuchsia_device_manager_CoordinatorAddDevice_reply(txn, status);
}

static zx_status_t fidl_AddDeviceInvisible(void* ctx, zx_handle_t raw_rpc,
                                           const uint64_t* props_data, size_t props_count,
                                           const char* name_data, size_t name_size,
                                           uint32_t protocol_id,
                                           const char* driver_path_data, size_t driver_path_size,
                                           const char* args_data, size_t args_size,
                                           fidl_txn_t* txn) {
    auto parent = static_cast<Device*>(ctx);
    zx::channel rpc(raw_rpc);
    fbl::StringPiece name(name_data, name_size);
    fbl::StringPiece driver_path(driver_path_data, driver_path_size);
    fbl::StringPiece args(args_data, args_size);

    zx_status_t status = dc_add_device(parent, fbl::move(rpc), props_data, props_count, name,
                                       protocol_id, driver_path, args, true);
    return fuchsia_device_manager_CoordinatorAddDeviceInvisible_reply(txn, status);
}

static zx_status_t fidl_RemoveDevice(void* ctx, fidl_txn_t* txn) {
    auto dev = static_cast<Device*>(ctx);
    if (dc_in_suspend()) {
        log(ERROR, "devcoord: rpc: remove-device '%s' forbidden in suspend\n", dev->name);
        return fuchsia_device_manager_CoordinatorRemoveDevice_reply(txn, ZX_ERR_BAD_STATE);
    }

    log(RPC_IN, "devcoord: rpc: remove-device '%s'\n", dev->name);
    // TODO(teisenbe): dc_remove_device and the reply func can return errors.  We should probably
    // act on it, but the existing code being migrated does not.
    dc_remove_device(dev, false);
    fuchsia_device_manager_CoordinatorRemoveDevice_reply(txn, ZX_OK);

    // Return STOP to signal we are done with this channel
    return ZX_ERR_STOP;
}

static zx_status_t fidl_MakeVisible(void* ctx, fidl_txn_t* txn) {
    auto dev = static_cast<Device*>(ctx);
    if (dc_in_suspend()) {
        log(ERROR, "devcoord: rpc: make-visible '%s' forbidden in suspend\n", dev->name);
        return fuchsia_device_manager_CoordinatorMakeVisible_reply(txn, ZX_ERR_BAD_STATE);
    }
    log(RPC_IN, "devcoord: rpc: make-visible '%s'\n", dev->name);
    // TODO(teisenbe): dc_make_visible can return errors.  We should probably
    // act on it, but the existing code being migrated does not.
    dc_make_visible(dev);
    return fuchsia_device_manager_CoordinatorMakeVisible_reply(txn, ZX_OK);
}

static zx_status_t fidl_BindDevice(void* ctx, const char* driver_path_data, size_t driver_path_size,
                                   fidl_txn_t* txn) {
    auto dev = static_cast<Device*>(ctx);
    fbl::StringPiece driver_path(driver_path_data, driver_path_size);
    if (dc_in_suspend()) {
        log(ERROR, "devcoord: rpc: bind-device '%s' forbidden in suspend\n", dev->name);
        return fuchsia_device_manager_CoordinatorBindDevice_reply(txn, ZX_ERR_BAD_STATE);
    }
    log(RPC_IN, "devcoord: rpc: bind-device '%s'\n", dev->name);
    zx_status_t status = dc_bind_device(dev, driver_path);
    return fuchsia_device_manager_CoordinatorBindDevice_reply(txn, status);
}

static zx_status_t fidl_GetTopologicalPath(void* ctx, fidl_txn_t* txn) {
    char path[fuchsia_device_manager_PATH_MAX + 1];

    auto dev = static_cast<Device*>(ctx);
    zx_status_t status;
    if ((status = dc_get_topo_path(dev, path, sizeof(path))) != ZX_OK) {
        return fuchsia_device_manager_CoordinatorGetTopologicalPath_reply(txn, status, nullptr, 0);
    }
    return fuchsia_device_manager_CoordinatorGetTopologicalPath_reply(txn, ZX_OK,
                                                                      path, strlen(path));
}

static zx_status_t fidl_LoadFirmware(void* ctx, const char* fw_path_data, size_t fw_path_size,
                                     fidl_txn_t* txn) {
    auto dev = static_cast<Device*>(ctx);

    char fw_path[fuchsia_device_manager_PATH_MAX + 1];
    memcpy(fw_path, fw_path_data, fw_path_size);
    fw_path[fw_path_size] = 0;

    zx::vmo vmo;
    uint64_t size = 0;
    zx_status_t status;
    if ((status = dc_load_firmware(dev, fw_path, &vmo, &size)) != ZX_OK) {
        return fuchsia_device_manager_CoordinatorLoadFirmware_reply(txn, status,
                                                                    ZX_HANDLE_INVALID, 0);
    }

    return fuchsia_device_manager_CoordinatorLoadFirmware_reply(txn, ZX_OK, vmo.release(), size);
}

static zx_status_t fidl_GetMetadata(void* ctx, uint32_t key, fidl_txn_t* txn) {
    auto dev = static_cast<Device*>(ctx);

    uint8_t data[fuchsia_device_manager_METADATA_MAX];
    size_t actual = 0;
    zx_status_t status = dc_get_metadata(dev, key, data, sizeof(data), &actual);
    if (status != ZX_OK) {
        return fuchsia_device_manager_CoordinatorGetMetadata_reply(txn, status, nullptr, 0);
    }
    return fuchsia_device_manager_CoordinatorGetMetadata_reply(txn, status, data, actual);
}

static zx_status_t fidl_AddMetadata(void* ctx, uint32_t key,
                                    const uint8_t* data_data, size_t data_count, fidl_txn_t* txn) {
    static_assert(fuchsia_device_manager_METADATA_MAX <= UINT32_MAX);

    auto dev = static_cast<Device*>(ctx);
    zx_status_t status = dc_add_metadata(dev, key, data_data, static_cast<uint32_t>(data_count));
    return fuchsia_device_manager_CoordinatorAddMetadata_reply(txn, status);
}

static zx_status_t fidl_PublishMetadata(void* ctx, const char* device_path_data,
                                        size_t device_path_size, uint32_t key,
                                        const uint8_t* data_data, size_t data_count,
                                        fidl_txn_t* txn) {
    auto dev = static_cast<Device*>(ctx);

    char path[fuchsia_device_manager_PATH_MAX + 1];
    memcpy(path, device_path_data, device_path_size);
    path[device_path_size] = 0;

    zx_status_t status = dc_publish_metadata(dev, path, key, data_data,
                                             static_cast<uint32_t>(data_count));
    return fuchsia_device_manager_CoordinatorPublishMetadata_reply(txn, status);
}

static zx_status_t fidl_DmCommand(void* ctx, zx_handle_t raw_log_socket,
                                  const char* command_data, size_t command_size, fidl_txn_t* txn) {
    zx::socket log_socket(raw_log_socket);

    if (log_socket.is_valid()) {
        dmctl_socket = fbl::move(log_socket);
    }

    zx_status_t status = handle_dmctl_write(command_size, command_data);
    dmctl_socket.reset();
    return fuchsia_device_manager_CoordinatorDmCommand_reply(txn, status);
}

static zx_status_t fidl_DmOpenVirtcon(void* ctx, zx_handle_t raw_vc_receiver) {
    zx::channel vc_receiver(raw_vc_receiver);

    zx_handle_t h = vc_receiver.release();
    zx_channel_write(virtcon_open, 0, nullptr, 0, &h, 1);
    return ZX_OK;
}

static zx_status_t fidl_DmWatch(void* ctx, zx_handle_t raw_watcher) {
    zx::channel watcher(raw_watcher);

    dc_watch(fbl::move(watcher));
    return ZX_OK;
}

static zx_status_t fidl_DmMexec(void* ctx, zx_handle_t raw_kernel, zx_handle_t raw_bootdata) {
    zx::vmo kernel(raw_kernel);
    zx::vmo bootdata(raw_bootdata);

    dc_mexec(fbl::move(kernel), fbl::move(bootdata));
    return ZX_OK;
}

static fuchsia_device_manager_Coordinator_ops_t fidl_ops = {
    .AddDevice = fidl_AddDevice,
    .AddDeviceInvisible = fidl_AddDeviceInvisible,
    .RemoveDevice = fidl_RemoveDevice,
    .MakeVisible = fidl_MakeVisible,
    .BindDevice = fidl_BindDevice,
    .GetTopologicalPath = fidl_GetTopologicalPath,
    .LoadFirmware = fidl_LoadFirmware,
    .GetMetadata = fidl_GetMetadata,
    .AddMetadata = fidl_AddMetadata,
    .PublishMetadata = fidl_PublishMetadata,
    .DmCommand = fidl_DmCommand,
    .DmOpenVirtcon = fidl_DmOpenVirtcon,
    .DmWatch = fidl_DmWatch,
    .DmMexec = fidl_DmMexec,
};

static zx_status_t dc_handle_device_read(Device* dev) {
    uint8_t msg[8192];
    zx_handle_t hin[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = fbl::count_of(hin);

    if (dev->flags & DEV_CTX_DEAD) {
        log(ERROR, "devcoord: dev %p already dead (in read)\n", dev);
        return ZX_ERR_INTERNAL;
    }

    zx_status_t r;
    if ((r = dev->hrpc.read(0, &msg, msize, &msize, hin, hcount, &hcount)) != ZX_OK) {
        return r;
    }

    fidl_msg_t fidl_msg = {
        .bytes = msg,
        .handles = hin,
        .num_bytes = msize,
        .num_handles = hcount,
    };

    if (fidl_msg.num_bytes < sizeof(fidl_message_header_t)) {
        zx_handle_close_many(fidl_msg.handles, fidl_msg.num_handles);
        return ZX_ERR_IO;
    }

    auto hdr = static_cast<fidl_message_header_t*>(fidl_msg.bytes);
    // Check if we're receiving a Coordinator request
    if (hdr->ordinal >= fuchsia_device_manager_CoordinatorAddDeviceOrdinal &&
        hdr->ordinal <= fuchsia_device_manager_CoordinatorDmMexecOrdinal) {
        FidlTxn txn(dev->hrpc, hdr->txid);
        return fuchsia_device_manager_Coordinator_dispatch(dev, txn.fidl_txn(),
                                                           &fidl_msg, &fidl_ops);
    }

    // Check if we're receiving a Controller reply
    if (hdr->ordinal >= fuchsia_device_manager_ControllerCreateDeviceStubOrdinal &&
        hdr->ordinal <= fuchsia_device_manager_ControllerRemoveDeviceOrdinal) {

        fbl::unique_ptr<PendingOperation> pending = dev->pending.pop_front();
        if (pending == nullptr) {
            log(ERROR, "devcoord: rpc: spurious status message\n");
            return ZX_OK;
        }

        // TODO: Check txid on the message
        switch (pending->op()) {
        case PendingOperation::Op::kBind: {
            if (hdr->ordinal != fuchsia_device_manager_ControllerBindDriverOrdinal) {
                log(ERROR, "devcoord: rpc: bind-driver '%s' received wrong reply ordinal %08x\n",
                    dev->name, hdr->ordinal);
                return ZX_ERR_IO;
            }
            const char* err_msg = nullptr;
            r = fidl_decode_msg(&fuchsia_device_manager_ControllerBindDriverResponseTable,
                                &fidl_msg, &err_msg);
            if (r != ZX_OK) {
                log(ERROR, "devcoord: rpc: bind-driver '%s' received malformed reply: %s\n",
                    dev->name, err_msg);
                return ZX_ERR_IO;
            }
            auto resp = reinterpret_cast<fuchsia_device_manager_ControllerBindDriverResponse*>(
                    fidl_msg.bytes);
            if (resp->status != ZX_OK) {
                log(ERROR, "devcoord: rpc: bind-driver '%s' status %d\n", dev->name, resp->status);
            } else {
                dc_notify(dev, DEVMGR_OP_DEVICE_CHANGED);
            }
            //TODO: try next driver, clear BOUND flag
            break;
        }
        case PendingOperation::Op::kSuspend: {
            if (hdr->ordinal != fuchsia_device_manager_ControllerSuspendOrdinal) {
                log(ERROR, "devcoord: rpc: suspend '%s' received wrong reply ordinal %08x\n",
                    dev->name, hdr->ordinal);
                return ZX_ERR_IO;
            }
            const char* err_msg = nullptr;
            r = fidl_decode_msg(&fuchsia_device_manager_ControllerSuspendResponseTable, &fidl_msg,
                                &err_msg);
            if (r != ZX_OK) {
                log(ERROR, "devcoord: rpc: suspend '%s' received malformed reply: %s\n",
                    dev->name, err_msg);
                return ZX_ERR_IO;
            }
            auto resp = reinterpret_cast<fuchsia_device_manager_ControllerSuspendResponse*>(
                    fidl_msg.bytes);
            if (resp->status != ZX_OK) {
                log(ERROR, "devcoord: rpc: suspend '%s' status %d\n", dev->name, resp->status);
            }
            auto ctx = static_cast<SuspendContext*>(pending->context());
            ctx->set_status(resp->status);
            dc_continue_suspend(ctx);
            break;
        }
        default:
            log(ERROR, "devcoord: rpc: dev '%s' received wrong unexpected reply %08x\n",
                dev->name, hdr->ordinal);
            return ZX_ERR_IO;
        }
        return ZX_OK;
    }

    log(ERROR, "devcoord: invalid rpc op %08x\n", hdr->ordinal);
    zx_handle_close_many(fidl_msg.handles, fidl_msg.num_handles);
    // This will cause the channel to be closed
    return ZX_ERR_NOT_SUPPORTED;
}

// handle inbound messages from devhost to devices
void Device::HandleRpc(Device* dev, async_dispatcher_t* dispatcher,
                      async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        log(ERROR, "devcoord: Device::HandleRpc aborting, saw status %d\n", status);
        return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
        zx_status_t r;
        if ((r = dc_handle_device_read(dev)) < 0) {
            if (r != ZX_ERR_STOP) {
                log(ERROR, "devcoord: device %p name='%s' rpc status: %d\n",
                    dev, dev->name, r);
            }
            dc_remove_device(dev, true);
            // Do not start waiting again on this device's channel again
            return;
        }
        Device::BeginWait(dev, dispatcher);
        return;
    }
    if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
        log(ERROR, "devcoord: device %p name='%s' disconnected!\n", dev, dev->name);
        dc_remove_device(dev, true);
        // Do not start waiting again on this device's channel again
        return;
    }
    log(ERROR, "devcoord: no work? %08x\n", signal->observed);
    Device::BeginWait(dev, dispatcher);
}

// send message to devhost, requesting the creation of a device
static zx_status_t dh_create_device(Device* dev, Devhost* dh,
                                    const char* args, zx::handle rpc_proxy) {
    zx_status_t r;

    zx::channel hrpc, hrpc_remote;
    if ((r = zx::channel::create(0, &hrpc, &hrpc_remote)) != ZX_OK) {
        return r;
    }

    if (dev->libname[0]) {
        zx::vmo vmo;
        if ((r = libname_to_vmo(dev->libname, &vmo)) != ZX_OK) {
            return r;
        }

        r = dh_send_create_device(dev, dh, fbl::move(hrpc_remote), fbl::move(vmo), args,
                                  fbl::move(rpc_proxy));
        if (r != ZX_OK) {
            return r;
        }
    } else {
        r = dh_send_create_device_stub(dh, fbl::move(hrpc_remote), dev->protocol_id);
        if (r != ZX_OK) {
            return r;
        }
    }

    dev->wait.set_object(hrpc.get());
    dev->hrpc = fbl::move(hrpc);
    dev->wait.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    if ((r = dev->wait.Begin(DcAsyncLoop()->dispatcher())) != ZX_OK) {
        return r;
    }
    dev->host = dh;
    dh->AddRef();
    dh->devices.push_back(dev);
    return ZX_OK;
}

static zx_status_t dc_create_proxy(Device* parent) {
    static constexpr const char kLibSuffix[] = ".so";
    static constexpr const char kProxyLibSuffix[] = ".proxy.so";
    static constexpr size_t kLibSuffixLen = sizeof(kLibSuffix) - 1;
    static constexpr size_t kProxyLibSuffixLen = sizeof(kProxyLibSuffix) - 1;

    if (parent->proxy != nullptr) {
        return ZX_OK;
    }

    const size_t namelen = strlen(parent->name);
    size_t liblen = strlen(parent->libname);
    const size_t parent_liblen = liblen;

    // non-immortal devices, use foo.proxy.so for
    // their proxy devices instead of foo.so
    bool proxylib = !(parent->flags & DEV_CTX_IMMORTAL);

    if (proxylib) {
        if (liblen < kLibSuffixLen) {
            return ZX_ERR_INTERNAL;
        }
        // Switch from the normal library suffix to the proxy one.
        liblen = liblen - kLibSuffixLen + kProxyLibSuffixLen;
    }

    auto dev = fbl::make_unique<Device>();
    if (dev == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    dev->name_alloc_ = fbl::make_unique<char[]>(namelen + liblen + 2);
    if (dev->name_alloc_ == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    char* dst = dev->name_alloc_.get();
    memcpy(dst, parent->name, namelen + 1);
    dev->name = dst;

    dst = &dev->name_alloc_[namelen + 1];
    memcpy(dst, parent->libname, parent_liblen);
    if (proxylib) {
        memcpy(dst + parent_liblen - kLibSuffixLen, kProxyLibSuffix,
               kProxyLibSuffixLen + 1);
    }
    dev->libname = dst;

    dev->flags = DEV_CTX_PROXY;
    dev->protocol_id = parent->protocol_id;
    dev->parent = parent;
    dev->AddRef();
    parent->proxy = dev.get();
    parent->AddRef();
    log(DEVLC, "devcoord: dev %p name='%s' ++ref=%d (proxy)\n",
        parent, parent->name, parent->refcount_);
    // TODO(teisenbe/kulakowski): This should go away once we switch to refptrs
    // here
    __UNUSED auto ptr = dev.release();
    return ZX_OK;
}

// send message to devhost, requesting the binding of a driver to a device
static zx_status_t dh_bind_driver(Device* dev, const char* libname) {
    auto pending = fbl::make_unique<PendingOperation>(PendingOperation::Op::kBind, nullptr);
    if (pending == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    zx::vmo vmo;
    zx_status_t r;
    if ((r = libname_to_vmo(libname, &vmo)) < 0) {
        return r;
    }

    if ((r = dh_send_bind_driver(dev, libname, fbl::move(vmo))) != ZX_OK) {
        return r;
    }

    dev->flags |= DEV_CTX_BOUND;

    dev->pending.push_back(fbl::move(pending));
    return ZX_OK;
}

static zx_status_t dc_prepare_proxy(Device* dev) {
    if (dev->flags & DEV_CTX_PROXY) {
        log(ERROR, "devcoord: cannot proxy a proxy: %s\n", dev->name);
        return ZX_ERR_INTERNAL;
    }

    // proxy args are "processname,args"
    const char* arg0 = dev->args.get();
    const char* arg1 = strchr(arg0, ',');
    if (arg1 == nullptr) {
        return ZX_ERR_INTERNAL;
    }
    size_t arg0len = arg1 - arg0;
    arg1++;

    char devhostname[32];
    snprintf(devhostname, sizeof(devhostname), "devhost:%.*s", (int) arg0len, arg0);

    zx_status_t r;
    if ((r = dc_create_proxy(dev)) < 0) {
        log(ERROR, "devcoord: cannot create proxy device: %d\n", r);
        return r;
    }

    // if this device has no devhost, first instantiate it
    if (dev->proxy->host == nullptr) {
        zx::channel h0;
        // May be either a VMO or a channel.
        zx::handle h1;

        // the immortal root devices do not provide proxy rpc
        bool need_proxy_rpc = !(dev->flags & DEV_CTX_IMMORTAL);

        if (need_proxy_rpc) {
            // create rpc channel for proxy device to talk to the busdev it proxys
            zx::channel c1;
            if ((r = zx::channel::create(0, &h0, &c1)) < 0) {
                log(ERROR, "devcoord: cannot create proxy rpc channel: %d\n", r);
                return r;
            }
            h1 = fbl::move(c1);
        } else if (dev == &sys_device) {
            // pass bootdata VMO handle to sys device
            h1 = fbl::move(bootdata_vmo);
        }
        if ((r = dc_new_devhost(devhostname, dev->host,
                                &dev->proxy->host)) < 0) {
            log(ERROR, "devcoord: dc_new_devhost: %d\n", r);
            return r;
        }
        if ((r = dh_create_device(dev->proxy, dev->proxy->host, arg1, fbl::move(h1))) < 0) {
            log(ERROR, "devcoord: dh_create_device: %d\n", r);
            return r;
        }
        if (need_proxy_rpc) {
            if ((r = dh_send_connect_proxy(dev, fbl::move(h0))) < 0) {
                log(ERROR, "devcoord: dh_send_connect_proxy: %d\n", r);
            }
        }
    }

    return ZX_OK;
}

static zx_status_t dc_attempt_bind(const Driver* drv, Device* dev) {
    // cannot bind driver to already bound device
    if ((dev->flags & DEV_CTX_BOUND) && (!(dev->flags & DEV_CTX_MULTI_BIND))) {
        return ZX_ERR_BAD_STATE;
    }
    if (!(dev->flags & DEV_CTX_MUST_ISOLATE)) {
        // non-busdev is pretty simple
        if (dev->host == nullptr) {
            log(ERROR, "devcoord: can't bind to device without devhost\n");
            return ZX_ERR_BAD_STATE;
        }
        return dh_bind_driver(dev, drv->libname.c_str());
    }

    zx_status_t r;
    if ((r = dc_prepare_proxy(dev)) < 0) {
        return r;
    }

    r = dh_bind_driver(dev->proxy, drv->libname.c_str());
    //TODO(swetland): arrange to mark us unbound when the proxy (or its devhost) goes away
    if ((r == ZX_OK) && !(dev->flags & DEV_CTX_MULTI_BIND)) {
        dev->flags |= DEV_CTX_BOUND;
    }
    return r;
}

static void dc_handle_new_device(Device* dev) {
    for (auto& drv : list_drivers) {
        if (dc_is_bindable(&drv, dev->protocol_id,
                           dev->props.get(), dev->prop_count, true)) {
            log(SPEW, "devcoord: drv='%s' bindable to dev='%s'\n",
                drv.name.c_str(), dev->name);

            dc_attempt_bind(&drv, dev);
            if (!(dev->flags & DEV_CTX_MULTI_BIND)) {
                break;
            }
        }
    }
}

static void dc_suspend_fallback(uint32_t flags) {
    log(INFO, "devcoord: suspend fallback with flags 0x%08x\n", flags);
    if (flags == DEVICE_SUSPEND_FLAG_REBOOT) {
        zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_REBOOT, nullptr);
    } else if (flags == DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER) {
        zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_REBOOT_BOOTLOADER, nullptr);
    } else if (flags == DEVICE_SUSPEND_FLAG_REBOOT_RECOVERY) {
        zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_REBOOT_RECOVERY, nullptr);
    } else if (flags == DEVICE_SUSPEND_FLAG_POWEROFF) {
        zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_SHUTDOWN, nullptr);
    }
}

static zx_status_t dc_suspend_devhost(Devhost* dh, SuspendContext* ctx) {
    if (dh->devices.is_empty()) {
        return ZX_OK;
    }
    Device* dev = &dh->devices.front();

    if (!(dev->flags & DEV_CTX_PROXY)) {
        log(INFO, "devcoord: devhost root '%s' (%p) is not a proxy\n",
            dev->name, dev);
        return ZX_ERR_BAD_STATE;
    }

    log(DEVLC, "devcoord: suspend devhost %p device '%s' (%p)\n",
        dh, dev->name, dev);

    zx_status_t r;
    if ((r = dh_send_suspend(dev, ctx->sflags())) != ZX_OK) {
        return r;
    }

    dh->flags |= DEV_HOST_SUSPEND;

    auto pending = fbl::make_unique<PendingOperation>(PendingOperation::Op::kSuspend, ctx);
    if (pending == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    dev->pending.push_back(fbl::move(pending));

    // TODO(teisenbe/kulakowski) Make SuspendContext automatically refcounted.
    ctx->AddRef();

    return ZX_OK;
}

static void append_suspend_list(SuspendContext* ctx, Devhost* dh) {
    // suspend order is children first
    for (auto& child : dh->children) {
        ctx->devhosts().push_front(&child);
    }
    for (auto& child : dh->children) {
        append_suspend_list(ctx, &child);
    }
}

// Returns the devhost at the front of the queue.
static Devhost* build_suspend_list(SuspendContext* ctx) {
    // sys_device must suspend last as on x86 it invokes
    // ACPI S-state transition
    ctx->devhosts().push_front(sys_device.proxy->host);
    append_suspend_list(ctx, sys_device.proxy->host);

    ctx->devhosts().push_front(root_device.proxy->host);
    append_suspend_list(ctx, root_device.proxy->host);

    ctx->devhosts().push_front(misc_device.proxy->host);
    append_suspend_list(ctx, misc_device.proxy->host);

    // test devices do not (yet) participate in suspend

    return &ctx->devhosts().front();
}

static void process_suspend_list(SuspendContext* ctx) {
    auto dh = ctx->devhosts().make_iterator(*ctx->dh());
    Devhost* parent = nullptr;
    do {
        if (!parent || (dh->parent == parent)) {
            // send Message::Op::kSuspend each set of children of a devhost at a time,
            // since they can run in parallel
            dc_suspend_devhost(dh.CopyPointer(), &suspend_ctx);
            parent = dh->parent;
        } else {
            // if the parent is different than the previous devhost's
            // parent, either this devhost is the parent, a child of
            // its parent's sibling, or the parent's sibling, so stop
            // processing until all the outstanding suspends are done
            parent = nullptr;
            break;
        }
    } while (++dh != ctx->devhosts().end());
    // next devhost to process once all the outstanding suspends are done
    if (dh.IsValid()) {
        ctx->set_dh(dh.CopyPointer());
    } else {
        ctx->set_dh(nullptr);
        ctx->devhosts().clear();
    }
}

static bool check_pending(const Device* dev) {
    const PendingOperation* pending = nullptr;
    if (dev->proxy) {
        if (!dev->proxy->pending.is_empty()) {
            pending = &dev->proxy->pending.back();
        }
    } else {
        if (!dev->pending.is_empty()) {
            pending = &dev->pending.back();
        }
    }
    if ((pending == nullptr) || (pending->op() != PendingOperation::Op::kSuspend)) {
        return false;
    } else {
        log(ERROR, "  devhost with device '%s' timed out\n", dev->name);
        return true;
    }
}

static int suspend_timeout_thread(void* arg) {
    // 10 seconds
    zx_nanosleep(zx_deadline_after(ZX_SEC(10)));

    auto ctx = static_cast<SuspendContext*>(arg);
    if (suspend_debug) {
        if (ctx->flags() == SuspendContext::Flags::kRunning) {
            return 0; // success
        }
        log(ERROR, "devcoord: suspend time out\n");
        log(ERROR, "  sflags: 0x%08x\n", ctx->sflags());
        for (const auto& dev : list_devices) {
            check_pending(&dev);
        }
        check_pending(&root_device);
        check_pending(&misc_device);
        check_pending(&sys_device);
    }
    if (suspend_fallback) {
        dc_suspend_fallback(ctx->sflags());
    }
    return 0;
}

static void dc_suspend(uint32_t flags) {
    // these top level devices should all have proxies. if not,
    // the system hasn't fully initialized yet and cannot go to
    // suspend.
    if (!sys_device.proxy || !root_device.proxy || !misc_device.proxy) {
        return;
    }

    SuspendContext* ctx = &suspend_ctx;
    if (ctx->flags() == SuspendContext::Flags::kSuspend) {
        return;
    }
    // Move the socket in to prevent the rpc handler from closing the handle.
    *ctx = SuspendContext(SuspendContext::Flags::kSuspend, flags, fbl::move(dmctl_socket));

    ctx->set_dh(build_suspend_list(ctx));

    if (suspend_fallback || suspend_debug) {
        thrd_t t;
        int ret = thrd_create_with_name(&t, suspend_timeout_thread, ctx,
                                        "devcoord-suspend-timeout");
        if (ret != thrd_success) {
            log(ERROR, "devcoord: can't create suspend timeout thread\n");
        }
    }

    process_suspend_list(ctx);
}

static void dc_mexec(zx::vmo kernel, zx::vmo bootdata) {
    // these top level devices should all have proxies. if not,
    // the system hasn't fully initialized yet and cannot mexec.
    if (!sys_device.proxy || !root_device.proxy || !misc_device.proxy) {
        return;
    }

    SuspendContext* ctx = &suspend_ctx;
    if (ctx->flags() == SuspendContext::Flags::kSuspend) {
        return;
    }
    *ctx = SuspendContext(SuspendContext::Flags::kSuspend, DEVICE_SUSPEND_FLAG_MEXEC,
                          zx::socket(), fbl::move(kernel), fbl::move(bootdata));

    ctx->set_dh(build_suspend_list(ctx));

    if (suspend_fallback || suspend_debug) {
        thrd_t t;
        int ret = thrd_create_with_name(&t, suspend_timeout_thread, ctx,
                                        "devcoord-suspend-timeout");
        if (ret != thrd_success) {
            log(ERROR, "devcoord: can't create suspend timeout thread\n");
        }
    }

    process_suspend_list(ctx);
}

static void dc_continue_suspend(SuspendContext* ctx) {
    if (ctx->status() != ZX_OK) {
        // TODO: unroll suspend
        // do not continue to suspend as this indicates a driver suspend
        // problem and should show as a bug
        log(ERROR, "devcoord: failed to suspend\n");
        // notify dmctl
        ctx->CloseSocket();
        if (ctx->sflags() == DEVICE_SUSPEND_FLAG_MEXEC) {
            ctx->kernel().signal(0, ZX_USER_SIGNAL_0);
        }
        ctx->set_flags(SuspendContext::Flags::kRunning);
        return;
    }

    if (ctx->Release()) {
        if (ctx->dh() != nullptr) {
            process_suspend_list(ctx);
        } else if (ctx->sflags() == DEVICE_SUSPEND_FLAG_MEXEC) {
            zx_system_mexec(get_root_resource(), ctx->kernel().get(), ctx->bootdata().get());
        } else {
            // should never get here on x86
            // on arm, if the platform driver does not implement
            // suspend go to the kernel fallback
            dc_suspend_fallback(ctx->sflags());
            // this handle is leaked on the shutdown path for x86
            ctx->CloseSocket();
            // if we get here the system did not suspend successfully
            ctx->set_flags(SuspendContext::Flags::kRunning);
        }
    }
}

// device binding program that pure (parentless)
// misc devices use to get published in the misc devhost
static struct zx_bind_inst misc_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT);

static bool is_misc_driver(Driver* drv) {
    return (drv->binding_size == sizeof(misc_device_binding)) &&
        (memcmp(&misc_device_binding, drv->binding.get(), sizeof(misc_device_binding)) == 0);
}

// device binding program that pure (parentless)
// test devices use to get published in the test devhost
static struct zx_bind_inst test_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT);

static bool is_test_driver(Driver* drv) {
    return (drv->binding_size == sizeof(test_device_binding)) &&
        (memcmp(&test_device_binding, drv->binding.get(), sizeof(test_device_binding)) == 0);
}


// device binding program that special root-level
// devices use to get published in the root devhost
static struct zx_bind_inst root_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_ROOT);

static bool is_root_driver(Driver* drv) {
    return (drv->binding_size == sizeof(root_device_binding)) &&
        (memcmp(&root_device_binding, drv->binding.get(), sizeof(root_device_binding)) == 0);
}

// dc_driver_added_init is called from driver enumeration during
// startup and before the devcoordinator starts running.  Enumerated
// drivers are added directly to the all-drivers or fallback list.
//
// TODO: fancier priorities
static void dc_driver_added_init(Driver* drv, const char* version) {
    if (version[0] == '*') {
        // fallback driver, load only if all else fails
        list_drivers_fallback.push_back(drv);
    } else if (version[0] == '!') {
        // debugging / development hack
        // prioritize drivers with version "!..." over others
        list_drivers.push_front(drv);
    } else {
        list_drivers.push_back(drv);
    }
}

static Work new_driver_work;

// dc_driver_added is called when a driver is added after the
// devcoordinator has started.  The driver is added to the new-drivers
// list and work is queued to process it.
static void dc_driver_added(Driver* drv, const char* version) {
    list_drivers_new.push_back(drv);
    if (new_driver_work.op == Work::Op::kIdle) {
        queue_work(&new_driver_work, Work::Op::kDriverAdded, 0);
    }
}

Device* coordinator_init(const zx::job& root_job) {
    printf("coordinator_init()\n");

    zx_status_t status = zx::job::create(root_job, 0u, &devhost_job);
    if (status < 0) {
        log(ERROR, "devcoord: unable to create devhost job\n");
    }
    static const zx_policy_basic_t policy[] = {
        { ZX_POL_BAD_HANDLE, ZX_POL_ACTION_EXCEPTION },
    };
    status = devhost_job.set_policy(ZX_JOB_POL_RELATIVE,
                                    ZX_JOB_POL_BASIC, &policy, fbl::count_of(policy));
    if (status < 0) {
        log(ERROR, "devcoord: zx_job_set_policy() failed\n");
    }
    devhost_job.set_property(ZX_PROP_NAME, "zircon-drivers", 15);

    return &root_device;
}

// dc_bind_driver is called when a new driver becomes available to
// the devcoordinator.  Existing devices are inspected to see if the
// new driver is bindable to them (unless they are already bound).
void dc_bind_driver(Driver* drv) {
    if (dc_running) {
        printf("devcoord: driver '%s' added\n", drv->name.c_str());
    }
    if (is_root_driver(drv)) {
        dc_attempt_bind(drv, &root_device);
    } else if (is_misc_driver(drv)) {
        dc_attempt_bind(drv, &misc_device);
    } else if (is_test_driver(drv)) {
        dc_attempt_bind(drv, &test_device);
    } else if (dc_running) {
        for (auto& dev : list_devices) {
            if (dev.flags & (DEV_CTX_BOUND | DEV_CTX_DEAD |
                             DEV_CTX_ZOMBIE | DEV_CTX_INVISIBLE)) {
                // if device is already bound or being destroyed or invisible, skip it
                continue;
            }
            if (dc_is_bindable(drv, dev.protocol_id,
                               dev.props.get(), dev.prop_count, true)) {
                log(INFO, "devcoord: drv='%s' bindable to dev='%s'\n",
                    drv->name.c_str(), dev.name);

                dc_attempt_bind(drv, &dev);
            }
        }
    }
}

void dc_handle_new_driver() {
    Driver* drv;
    while ((drv = list_drivers_new.pop_front()) != nullptr) {
        list_drivers.push_back(drv);
        dc_bind_driver(drv);
    }
}

#define CTL_SCAN_SYSTEM 1
#define CTL_ADD_SYSTEM 2

static bool system_available;
static bool system_loaded;

// List of drivers loaded from /system by system_driver_loader()
static fbl::DoublyLinkedList<Driver*, Driver::Node> list_drivers_system;

static int system_driver_loader(void* arg);

static void dc_scan_system() {
    if (!system_loaded) {
        system_loaded = true;
        // Fire up a thread to scan/load system drivers
        // This avoids deadlocks between the devhosts hosting the block devices
        // that these drivers may be served from and the devcoordinator loading them.
        thrd_t t;
        thrd_create_with_name(&t, system_driver_loader, nullptr, "system-driver-loader");
    }
}

static void dc_control_event(async_dispatcher_t* dispatcher, async::Receiver* receiver,
                             zx_status_t status, const zx_packet_user_t* data) {
    if (status != ZX_OK) {
        log(ERROR, "devcoord: dc_control_event aborting, saw status %d\n", status);
        return;
    }

    switch (data->u32[0]) {
    case CTL_SCAN_SYSTEM:
        dc_scan_system();
        break;
    case CTL_ADD_SYSTEM: {
        Driver* drv;
        // Add system drivers to the new list
        while ((drv = list_drivers_system.pop_front()) != nullptr) {
            list_drivers_new.push_back(drv);
        }
        // Add any remaining fallback drivers to the new list
        while ((drv = list_drivers_fallback.pop_back()) != nullptr) {
            printf("devcoord: fallback driver '%s' is available\n", drv->name.c_str());
            list_drivers_new.push_back(drv);
        }
        // Queue Driver Added work if not already queued
        if (new_driver_work.op == Work::Op::kIdle) {
            queue_work(&new_driver_work, Work::Op::kDriverAdded, 0);
        }
        break;
    }
    }
}

static async::Receiver control_handler(dc_control_event);

// Drivers added during system scan (from the dedicated thread)
// are added to list_drivers_system for bulk processing once
// CTL_ADD_SYSTEM is sent.
//
// TODO: fancier priority management
static void dc_driver_added_sys(Driver* drv, const char* version) {
    log(INFO, "devmgr: adding system driver '%s' '%s'\n", drv->name.c_str(), drv->libname.c_str());

    if (load_vmo(drv->libname.c_str(), &drv->dso_vmo)) {
        log(ERROR, "devmgr: system driver '%s' '%s' could not cache DSO\n", drv->name.c_str(),
            drv->libname.c_str());
    }
    if (version[0] == '*') {
        // de-prioritize drivers that are "fallback"
        list_drivers_system.push_back(drv);
    } else {
        list_drivers_system.push_front(drv);
    }
}

static int system_driver_loader(void* arg) {
    find_loadable_drivers("/system/driver", dc_driver_added_sys);
    find_loadable_drivers("/system/lib/driver", dc_driver_added_sys);

    zx_packet_user_t pkt = {};
    pkt.u32[0] = CTL_ADD_SYSTEM;
    control_handler.QueuePacket(DcAsyncLoop()->dispatcher(), &pkt);
    return 0;
}

void load_system_drivers() {
    system_available = true;

    zx_packet_user_t pkt = {};
    pkt.u32[0] = CTL_SCAN_SYSTEM;
    control_handler.QueuePacket(DcAsyncLoop()->dispatcher(), &pkt);
}

void coordinator() {
    log(INFO, "devmgr: coordinator()\n");

    if (getenv_bool("devmgr.verbose", false)) {
        log_flags |= LOG_ALL;
    }

    suspend_fallback = getenv_bool("devmgr.suspend-timeout-fallback", false);
    suspend_debug = getenv_bool("devmgr.suspend-timeout-debug", false);

    dc_asan_drivers = getenv_bool("devmgr.devhost.asan", false);

    zx_status_t status = initialize_core_devices();
    if (status != ZX_OK) {
        log(ERROR, "devmgr: failed to initialize core devices\n");
        return;
    }

    devfs_publish(&root_device, &misc_device);
    devfs_publish(&root_device, &sys_device);
    devfs_publish(&root_device, &test_device);

    find_loadable_drivers("/boot/driver", dc_driver_added_init);
    find_loadable_drivers("/boot/driver/test", dc_driver_added_init);
    find_loadable_drivers("/boot/lib/driver", dc_driver_added_init);

    // Special case early handling for the ramdisk boot
    // path where /system is present before the coordinator
    // starts.  This avoids breaking the "priority hack" and
    // can be removed once the real driver priority system
    // exists.
    if (system_available) {
        dc_scan_system();
    }

    // x86 platforms use acpi as the system device
    // all other platforms use the platform bus
#if defined(__x86_64__)
    sys_device.libname = "/boot/driver/bus-acpi.so";
#else
    sys_device.libname = "/boot/driver/platform-bus.so";
#endif
    dc_prepare_proxy(&sys_device);
    dc_prepare_proxy(&test_device);

    if (require_system && !system_loaded) {
        printf("devcoord: full system required, ignoring fallback drivers until /system is loaded\n");
    } else {
        Driver* drv;
        while ((drv = list_drivers_fallback.pop_back()) != nullptr) {
            list_drivers.push_back(drv);
        }
    }

    // Initial bind attempt for drivers enumerated at startup.
    for (auto& drv : list_drivers) {
        dc_bind_driver(&drv);
    }

    dc_running = true;

    for (;;) {
        zx_status_t status;
        if (list_pending_work.is_empty()) {
            status = DcAsyncLoop()->Run(zx::time::infinite(), true /* once */);
        } else {
            status = DcAsyncLoop()->Run(zx::time(), true /* once */);
            if (status == ZX_ERR_TIMED_OUT) {
                auto work = list_pending_work.pop_front();
                process_work(work);
                continue;
            }
        }
        if (status != ZX_OK) {
            log(ERROR, "devcoord: port dispatch ended: %d\n", status);
        }
    }
}

} // namespace devmgr
