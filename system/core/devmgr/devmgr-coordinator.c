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
#include <launchpad/launchpad.h>
#include <zircon/assert.h>
#include <zircon/ktrace.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/system.h>
#include <zircon/device/dmctl.h>
#include <zircon/boot/bootdata.h>
#include <lib/fdio/io.h>

#include "devcoordinator.h"
#include "devmgr.h"
#include "log.h"
#include "memfs-private.h"

static void dc_driver_added(driver_t* drv, const char* version);
static void dc_driver_added_init(driver_t* drv, const char* version);


#define BOOT_FIRMWARE_DIR "/boot/lib/firmware"
#define SYSTEM_FIRMWARE_DIR "/system/lib/firmware"

extern zx_handle_t virtcon_open;

uint32_t log_flags = LOG_ERROR | LOG_INFO;

bool dc_asan_drivers = false;
bool dc_launched_first_devhost = false;

static zx_handle_t bootdata_vmo;

static void dc_dump_state(void);
static void dc_dump_devprops(void);
static void dc_dump_drivers(void);

typedef struct {
    zx_status_t status;
    uint32_t flags;
#define RUNNING 0
#define SUSPEND 1
    uint32_t sflags;    // suspend flags
    uint32_t count;     // outstanding msgs
    devhost_t* dh;      // next devhost to process
    list_node_t devhosts;

    zx_handle_t socket; // socket to notify on for 'dm reboot' and 'dm poweroff'

    // mexec arguments
    zx_handle_t kernel;
    zx_handle_t bootdata;
} suspend_context_t;
static suspend_context_t suspend_ctx = {
    .devhosts = LIST_INITIAL_VALUE(suspend_ctx.devhosts),
};

typedef struct {
    list_node_t node;
    uint32_t type;
    uint32_t length;
    bool has_path;      // zero terminated string starts at data[length]
    uint8_t data[];
} dc_metadata_t;

static list_node_t published_metadata = LIST_INITIAL_VALUE(published_metadata);

static bool dc_in_suspend(void) {
    return !!suspend_ctx.flags;
}
static void dc_suspend(uint32_t flags);
static void dc_mexec(zx_handle_t* h);
static void dc_continue_suspend(suspend_context_t* ctx);

static bool suspend_fallback = false;
static bool suspend_debug = false;

static device_t root_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND,
    .protocol_id = ZX_PROTOCOL_ROOT,
    .name = "root",
    .libname = "",
    .args = "root,",
    .children = LIST_INITIAL_VALUE(root_device.children),
    .pending = LIST_INITIAL_VALUE(root_device.pending),
    .metadata = LIST_INITIAL_VALUE(root_device.metadata),
    .refcount = 1,
};

static device_t misc_device = {
    .parent = &root_device,
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND,
    .protocol_id = ZX_PROTOCOL_MISC_PARENT,
    .name = "misc",
    .libname = "",
    .args = "misc,",
    .children = LIST_INITIAL_VALUE(misc_device.children),
    .pending = LIST_INITIAL_VALUE(misc_device.pending),
    .metadata = LIST_INITIAL_VALUE(misc_device.metadata),
    .refcount = 1,
};

static device_t sys_device = {
    .parent = &root_device,
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE,
    .name = "sys",
    .libname = "",
    .args = "sys,",
    .children = LIST_INITIAL_VALUE(sys_device.children),
    .pending = LIST_INITIAL_VALUE(sys_device.pending),
    .metadata = LIST_INITIAL_VALUE(sys_device.metadata),
    .refcount = 1,
};

static device_t test_device = {
    .parent = &root_device,
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND,
    .protocol_id = ZX_PROTOCOL_TEST_PARENT,
    .name = "test",
    .libname = "",
    .args = "test,",
    .children = LIST_INITIAL_VALUE(test_device.children),
    .pending = LIST_INITIAL_VALUE(test_device.pending),
    .metadata = LIST_INITIAL_VALUE(test_device.metadata),
    .refcount = 1,
};


static zx_handle_t dmctl_socket;

static void dmprintf(const char* fmt, ...) {
    if (dmctl_socket == ZX_HANDLE_INVALID) {
        return;
    }
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t actual;
    if (zx_socket_write(dmctl_socket, 0, buf, strlen(buf), &actual) < 0) {
        zx_handle_close(dmctl_socket);
        dmctl_socket = ZX_HANDLE_INVALID;
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
    if ((len == 7) && !memcmp(cmd, "drivers", 7)) {
        dc_dump_drivers();
        return ZX_OK;
    }
    if (len == 8) {
        if (!memcmp(cmd, "poweroff", 8) || !memcmp(cmd, "shutdown", 8)) {
            devmgr_vfs_exit();
            dc_suspend(DEVICE_SUSPEND_FLAG_POWEROFF);
            return ZX_OK;
        }
        if (!memcmp(cmd, "ktraceon", 8)) {
            zx_ktrace_control(get_root_resource(), KTRACE_ACTION_START, KTRACE_GRP_ALL, NULL);
            return ZX_OK;
        }
        if (!memcmp(cmd, "devprops", 8)) {
            dc_dump_devprops();
            return ZX_OK;
        }
    }
    if ((len == 9) && (!memcmp(cmd, "ktraceoff", 9))) {
        zx_ktrace_control(get_root_resource(), KTRACE_ACTION_STOP, 0, NULL);
        zx_ktrace_control(get_root_resource(), KTRACE_ACTION_REWIND, 0, NULL);
        return ZX_OK;
    }
    if ((len > 12) && !memcmp(cmd, "kerneldebug ", 12)) {
        return zx_debug_send_command(get_root_resource(), cmd + 12, len - 12);
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

//TODO: these are copied from devhost.h
#define ID_HJOBROOT 4
zx_handle_t get_sysinfo_job_root(void);


static zx_status_t dc_handle_device(port_handler_t* ph, zx_signals_t signals, uint32_t evt);
static zx_status_t dc_attempt_bind(driver_t* drv, device_t* dev);

static bool dc_running;

static zx_handle_t dc_watch_channel;

static zx_handle_t devhost_job;
port_t dc_port;

// All Drivers
static list_node_t list_drivers = LIST_INITIAL_VALUE(list_drivers);

// Drivers to add to All Drivers
static list_node_t list_drivers_new = LIST_INITIAL_VALUE(list_drivers_new);

// Drivers to try last
static list_node_t list_drivers_fallback = LIST_INITIAL_VALUE(list_drivers_fallback);

// All Devices (excluding static immortal devices)
static list_node_t list_devices = LIST_INITIAL_VALUE(list_devices);

// All DevHosts
static list_node_t list_devhosts = LIST_INITIAL_VALUE(list_devhosts);

static driver_t* libname_to_driver(const char* libname) {
    driver_t* drv;
    list_for_every_entry(&list_drivers, drv, driver_t, node) {
        if (!strcmp(libname, drv->libname)) {
            return drv;
        }
    }
    return NULL;
}

static zx_status_t load_vmo(const char* libname, zx_handle_t* out) {
    int fd = open(libname, O_RDONLY);
    if (fd < 0) {
        log(ERROR, "devcoord: cannot open driver '%s'\n", libname);
        return ZX_ERR_IO;
    }
    zx_status_t r = fdio_get_vmo_clone(fd, out);
    close(fd);
    if (r < 0) {
        log(ERROR, "devcoord: cannot get driver vmo '%s'\n", libname);
    }
    const char* vmo_name = strrchr(libname, '/');
    if (vmo_name != NULL) {
        ++vmo_name;
    } else {
        vmo_name = libname;
    }
    zx_object_set_property(*out, ZX_PROP_NAME, vmo_name, strlen(vmo_name));
    return r;
}

static zx_status_t libname_to_vmo(const char* libname, zx_handle_t* out) {
    driver_t* drv = libname_to_driver(libname);
    if (drv == NULL) {
        log(ERROR, "devcoord: cannot find driver '%s'\n", libname);
        return ZX_ERR_NOT_FOUND;
    }

    // Check for cached DSO
    if (drv->dso_vmo != ZX_HANDLE_INVALID) {
        zx_status_t r = zx_handle_duplicate(drv->dso_vmo,
                                            ZX_RIGHTS_BASIC | ZX_RIGHTS_PROPERTY |
                                            ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP,
                                            out);
        if (r != ZX_OK) {
            log(ERROR, "devcoord: cannot duplicate cached dso for '%s' '%s'\n", drv->name, libname);
        }
        return r;
    } else {
        return load_vmo(libname, out);
    }
}

void devmgr_set_bootdata(zx_handle_t vmo) {
    if (bootdata_vmo == ZX_HANDLE_INVALID) {
        zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &bootdata_vmo);
    }
}

static void dc_dump_device(device_t* dev, size_t indent) {
    zx_koid_t pid = dev->host ? dev->host->koid : 0;
    char extra[256];
    if (log_flags & LOG_DEVLC) {
        snprintf(extra, sizeof(extra), " dev=%p ref=%d", dev, dev->refcount);
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
    device_t* child;
    if (dev->proxy) {
        indent++;
        dc_dump_device(dev->proxy, indent);
    }
    list_for_every_entry(&dev->children, child, device_t, node) {
        dc_dump_device(child, indent + 1);
    }
}

static void dc_dump_state(void) {
    dc_dump_device(&root_device, 0);
    dc_dump_device(&misc_device, 1);
    dc_dump_device(&sys_device, 1);
    dc_dump_device(&test_device, 1);
}

static void dc_dump_device_props(device_t* dev) {
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
            const zx_device_prop_t* p = dev->props + i;
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

    device_t* child;
    if (dev->proxy) {
        dc_dump_device_props(dev->proxy);
    }
    list_for_every_entry(&dev->children, child, device_t, node) {
        dc_dump_device_props(child);
    }
}

static void dc_dump_devprops(void) {
    dc_dump_device_props(&root_device);
    dc_dump_device_props(&misc_device);
    dc_dump_device_props(&sys_device);
    dc_dump_device_props(&test_device);
}

static void dc_dump_drivers(void) {
    driver_t* drv;
    bool first = true;
    list_for_every_entry(&list_drivers, drv, driver_t, node) {
        dmprintf("%sName    : %s\n", first ? "" : "\n", drv->name);
        dmprintf("Driver  : %s\n", drv->libname ? drv->libname : "(null)");
        dmprintf("Flags   : 0x%08x\n", drv->flags);
        if (drv->binding_size) {
            char line[256];
            uint32_t count = drv->binding_size / sizeof(drv->binding[0]);
            dmprintf("Binding : %u instruction%s (%u bytes)\n",
                     count, (count == 1) ? "" : "s", drv->binding_size);
            for (uint32_t i = 0; i < count; ++i) {
                di_dump_bind_inst(drv->binding + i, line, sizeof(line));
                dmprintf("[%u/%u]: %s\n", i + 1, count, line);
            }
        }
        first = false;
    }
}

static void dc_handle_new_device(device_t* dev);
static void dc_handle_new_driver(void);

#define WORK_IDLE 0
#define WORK_DEVICE_ADDED 1
#define WORK_DRIVER_ADDED 2

static list_node_t list_pending_work = LIST_INITIAL_VALUE(list_pending_work);
static list_node_t list_unbound_devices = LIST_INITIAL_VALUE(list_unbound_devices);

static void queue_work(work_t* work, uint32_t op, uint32_t arg) {
    ZX_ASSERT(work->op == WORK_IDLE);
    work->op = op;
    work->arg = arg;
    list_add_tail(&list_pending_work, &work->node);
}

static void cancel_work(work_t* work) {
    if (work->op != WORK_IDLE) {
        list_delete(&work->node);
        work->op = WORK_IDLE;
    }
}

static void process_work(work_t* work) {
    uint32_t op = work->op;
    work->op = WORK_IDLE;

    switch (op) {
    case WORK_DEVICE_ADDED: {
        device_t* dev = containerof(work, device_t, work);
        dc_handle_new_device(dev);
        break;
    }
    case WORK_DRIVER_ADDED: {
        dc_handle_new_driver();
        break;
    }
    default:
        log(ERROR, "devcoord: unknown work: op=%u\n", op);
    }
}

static const char* get_devhost_bin(void) {
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

zx_handle_t get_service_root(void);

static zx_status_t dc_get_topo_path(device_t* dev, char* out, size_t max) {
    char tmp[max];
    char* path = tmp + max - 1;
    *path = 0;
    size_t total = 1;

    while (dev != NULL) {
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
static zx_status_t dc_notify(device_t* dev, uint32_t op) {
    if (dc_watch_channel == ZX_HANDLE_INVALID) {
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t r;
    if (op == DEVMGR_OP_DEVICE_ADDED) {
        size_t propslen = sizeof(zx_device_prop_t) * dev->prop_count;
        size_t len = sizeof(devmgr_event_t) + propslen;
        char msg[len + DC_PATH_MAX];
        devmgr_event_t* evt = (void*) msg;
        memset(evt, 0, sizeof(devmgr_event_t));
        memcpy(msg + sizeof(devmgr_event_t), dev->props, propslen);
        if (dc_get_topo_path(dev, msg + len, DC_PATH_MAX) < 0) {
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
        evt->u.add.props_len = propslen;
        evt->u.add.path_len = pathlen;
        r = zx_channel_write(dc_watch_channel, 0, msg, len, NULL, 0);
    } else {
        devmgr_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.opcode = op;
        if (dev->flags & DEV_CTX_BOUND) {
            evt.flags |= DEVMGR_FLAGS_BOUND;
        }
        evt.id = (uintptr_t) dev;
        r = zx_channel_write(dc_watch_channel, 0, &evt, sizeof(evt), NULL, 0);
    }
    if (r < 0) {
        zx_handle_close(dc_watch_channel);
        dc_watch_channel = ZX_HANDLE_INVALID;
    }
    return r;
}

static void dc_watch(zx_handle_t h) {
    if (dc_watch_channel != ZX_HANDLE_INVALID) {
        zx_handle_close(dc_watch_channel);
    }
    dc_watch_channel = h;
    device_t* dev;
    list_for_every_entry(&list_devices, dev, device_t, anode) {
        if (dev->flags & (DEV_CTX_DEAD | DEV_CTX_ZOMBIE)) {
            // if device is dead, ignore it
            continue;
        }
        if (dc_notify(dev, DEVMGR_OP_DEVICE_ADDED) < 0) {
            break;
        }
    }
}

static zx_status_t dc_launch_devhost(devhost_t* host,
                                     const char* name, zx_handle_t hrpc) {
    const char* devhost_bin = get_devhost_bin();

    launchpad_t* lp;
    launchpad_create_with_jobs(devhost_job, 0, name, &lp);
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
    size_t name_count = 0;

    //TODO: eventually devhosts should not have vfs access
    launchpad_add_handle(lp, fs_clone("boot"),
                         PA_HND(PA_NS_DIR, name_count++));

    //TODO: constrain to /svc/device
    if ((h = fs_clone("svc")) != ZX_HANDLE_INVALID) {
        launchpad_add_handle(lp, h, PA_HND(PA_NS_DIR, name_count++));
    }

    launchpad_set_nametable(lp, name_count, nametable);

    //TODO: limit root job access to root devhost only
    launchpad_add_handle(lp, get_sysinfo_job_root(),
                         PA_HND(PA_USER0, ID_HJOBROOT));

    const char* errmsg;
    zx_status_t status = launchpad_go(lp, &host->proc, &errmsg);
    if (status < 0) {
        log(ERROR, "devcoord: launch devhost '%s': failed: %d: %s\n",
            name, status, errmsg);
        return status;
    }
    zx_info_handle_basic_t info;
    if (zx_object_get_info(host->proc, ZX_INFO_HANDLE_BASIC, &info,
                           sizeof(info), NULL, NULL) == ZX_OK) {
        host->koid = info.koid;
    }
    log(INFO, "devcoord: launch devhost '%s': pid=%zu\n",
        name, host->koid);

    dc_launched_first_devhost = true;

    return ZX_OK;
}

static zx_status_t dc_new_devhost(const char* name, devhost_t* parent,
                                  devhost_t** out) {
    devhost_t* dh = calloc(1, sizeof(devhost_t));
    if (dh == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_handle_t hrpc;
    zx_status_t r;
    if ((r = zx_channel_create(0, &hrpc, &dh->hrpc)) < 0) {
        free(dh);
        return r;
    }

    if ((r = dc_launch_devhost(dh, name, hrpc)) < 0) {
        zx_handle_close(dh->hrpc);
        free(dh);
        return r;
    }

    list_initialize(&dh->devices);
    list_initialize(&dh->children);

    if (parent) {
        dh->parent = parent;
        dh->parent->refcount++;
        list_add_tail(&dh->parent->children, &dh->node);
    }
    list_add_tail(&list_devhosts, &dh->anode);

    log(DEVLC, "devcoord: new host %p\n", dh);

    *out = dh;
    return ZX_OK;
}

static void dc_release_devhost(devhost_t* dh) {
    dh->refcount--;
    if (dh->refcount > 0) {
        return;
    }
    log(INFO, "devcoord: destroy host %p\n", dh);
    devhost_t* parent = dh->parent;
    if (parent != NULL) {
        dh->parent = NULL;
        list_delete(&dh->node);
        dc_release_devhost(parent);
    }
    list_delete(&dh->anode);
    zx_handle_close(dh->hrpc);
    zx_task_kill(dh->proc);
    zx_handle_close(dh->proc);
    free(dh);
}

// called when device children or proxys are removed
static void dc_release_device(device_t* dev) {
    log(DEVLC, "devcoord: release dev %p name='%s' ref=%d\n", dev, dev->name, dev->refcount);

    dev->refcount--;
    if (dev->refcount > 0) {
        return;
    }

    // Immortal devices are never destroyed
    if (dev->flags & DEV_CTX_IMMORTAL) {
        return;
    }

    log(DEVLC, "devcoord: destroy dev %p name='%s'\n", dev, dev->name);

    devfs_unpublish(dev);

    if (dev->hrpc != ZX_HANDLE_INVALID) {
        zx_handle_close(dev->hrpc);
        dev->hrpc = ZX_HANDLE_INVALID;
        dev->ph.handle = ZX_HANDLE_INVALID;
    }
    dev->host = NULL;

    cancel_work(&dev->work);

    dc_metadata_t* md;
    while ((md = list_remove_head_type(&dev->metadata, dc_metadata_t, node)) != NULL) {
        if (md->has_path) {
            // return to published_metadata list
            list_add_tail(&published_metadata, &md->node);
        } else {
            // metadata was attached directly to this device, so we free it here
            free(md);
        }
    }

    //TODO: cancel any pending rpc responses
    free(dev);
}

// Add a new device to a parent device (same devhost)
// New device is published in devfs.
// Caller closes handles on error, so we don't have to.
static zx_status_t dc_add_device(device_t* parent, zx_handle_t hrpc,
                                 dc_msg_t* msg, const char* name,
                                 const char* args, const void* data,
                                 bool invisible) {
    if (msg->datalen % sizeof(zx_device_prop_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    device_t* dev;
    // allocate device struct, followed by space for props, followed
    // by space for bus arguments, followed by space for the name
    size_t sz = sizeof(*dev) + msg->datalen + msg->argslen + msg->namelen + 2;
    if ((dev = calloc(1, sz)) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    list_initialize(&dev->children);
    list_initialize(&dev->pending);
    list_initialize(&dev->metadata);
    dev->hrpc = hrpc;
    dev->prop_count = msg->datalen / sizeof(zx_device_prop_t);
    dev->protocol_id = msg->protocol_id;

    char* text = (char*) (dev->props + dev->prop_count);
    memcpy(text, args, msg->argslen + 1);
    dev->args = text;

    text += msg->argslen + 1;
    memcpy(text, name, msg->namelen + 1);

    char* text2 = strchr(text, ',');
    if (text2 != NULL) {
        *text2++ = 0;
        dev->name = text2;
        dev->libname = text;
    } else {
        dev->name = text;
        dev->libname = "";
    }

    memcpy(dev->props, data, msg->datalen);

    if (strlen(dev->name) > ZX_DEVICE_NAME_MAX) {
        free(dev);
        return ZX_ERR_INVALID_ARGS;
    }

    // If we have bus device args we are,
    // by definition, a bus device.
    if (args[0]) {
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
    if ((r = devfs_publish(parent, dev)) < 0) {
        free(dev);
        return r;
    }

    dev->ph.handle = hrpc;
    dev->ph.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    dev->ph.func = dc_handle_device;
    if ((r = port_wait(&dc_port, &dev->ph)) < 0) {
        devfs_unpublish(dev);
        free(dev);
        return r;
    }

    if (dev->host) {
        //TODO host == NULL should be impossible
        dev->host->refcount++;
        list_add_tail(&dev->host->devices, &dev->dhnode);
    }
    dev->refcount = 1;
    list_add_tail(&parent->children, &dev->node);
    parent->refcount++;

    list_add_tail(&list_devices, &dev->anode);

    log(DEVLC, "devcoord: dev %p name='%s' ++ref=%d (child)\n",
        parent, parent->name, parent->refcount);

    log(DEVLC, "devcoord: publish %p '%s' props=%u args='%s' parent=%p\n",
        dev, dev->name, dev->prop_count, dev->args, dev->parent);

    if (!invisible) {
        dc_notify(dev, DEVMGR_OP_DEVICE_ADDED);
        queue_work(&dev->work, WORK_DEVICE_ADDED, 0);
    }
    return ZX_OK;
}

static zx_status_t dc_make_visible(device_t* dev) {
    if (dev->flags & DEV_CTX_DEAD) {
        return ZX_ERR_BAD_STATE;
    }
    if (dev->flags & DEV_CTX_INVISIBLE) {
        dev->flags &= ~DEV_CTX_INVISIBLE;
        devfs_advertise(dev);
        dc_notify(dev, DEVMGR_OP_DEVICE_ADDED);
        queue_work(&dev->work, WORK_DEVICE_ADDED, 0);
    }
    return ZX_OK;
}

// Remove device from parent
// forced indicates this is removal due to a channel close
// or process exit, which means we should remove all other
// devices that share the devhost at the same time
static zx_status_t dc_remove_device(device_t* dev, bool forced) {
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
        dc_msg_t msg;
        uint32_t mlen;
        zx_status_t r;
        if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, NULL, NULL)) < 0) {
            log(ERROR, "devcoord: dc_msg_pack failed in dc_remove_device\n");
        } else {
            msg.txid = 0;
            msg.op = DC_OP_REMOVE_DEVICE;
            if ((r = zx_channel_write(dev->proxy->hrpc, 0, &msg, mlen, NULL, 0)) != ZX_OK) {
            log(ERROR, "devcoord: zx_channel_write failed in dc_remove_devicey\n");
            }
        }
    }

    // detach from devhost
    devhost_t* dh = dev->host;
    if (dh != NULL) {
        dev->host = NULL;
        list_delete(&dev->dhnode);

        // If we are responding to a disconnect,
        // we'll remove all the other devices on this devhost too.
        // A side-effect of this is that the devhost will be released,
        // as well as any proxy devices.
        if (forced) {
            dh->flags |= DEV_HOST_DYING;

            device_t* next;
            device_t* last = NULL;
            while ((next = list_peek_head_type(&dh->devices, device_t, dhnode)) != NULL) {
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
    device_t* parent = dev->parent;
    if (parent != NULL) {
        dev->parent = NULL;
        if (dev->flags & DEV_CTX_PROXY) {
            parent->proxy = NULL;
        } else {
            list_delete(&dev->node);
            if (list_is_empty(&parent->children)) {
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
                    ((parent->host == NULL) || !(parent->host->flags & DEV_HOST_DYING))) {

                    log(DEVLC, "devcoord: bus device %p name='%s' is unbound\n",
                        parent, parent->name);

                    //TODO: introduce timeout, exponential backoff
                    queue_work(&parent->work, WORK_DEVICE_ADDED, 0);
                }
            }
        }
        dc_release_device(parent);
    }

    if (!(dev->flags & DEV_CTX_PROXY)) {
        // remove from list of all devices
        list_delete(&dev->anode);
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

static zx_status_t dc_bind_device(device_t* dev, const char* drvlibname) {
     log(INFO, "devcoord: dc_bind_device() '%s'\n", drvlibname);

    // shouldn't be possible to get a bind request for a proxy device
    if (dev->flags & DEV_CTX_PROXY) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // A libname of "" means a general rebind request
    // instead of a specific request
    bool autobind = (drvlibname[0] == 0);

    //TODO: disallow if we're in the middle of enumeration, etc
    driver_t* drv;
    list_for_every_entry(&list_drivers, drv, driver_t, node) {
        if (autobind || !strcmp(drv->libname, drvlibname)) {
            if (dc_is_bindable(drv, dev->protocol_id,
                               dev->props, dev->prop_count, autobind)) {
                log(SPEW, "devcoord: drv='%s' bindable to dev='%s'\n",
                    drv->name, dev->name);
                dc_attempt_bind(drv, dev);
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

static zx_status_t dc_load_firmware(device_t* dev, const char* path,
                                    zx_handle_t* vmo, size_t* size) {
    static const char* fwdirs[] = {
        BOOT_FIRMWARE_DIR,
        SYSTEM_FIRMWARE_DIR,
    };

    int fd, fwfd;
    for (unsigned n = 0; n < countof(fwdirs); n++) {
        if ((fd = open(fwdirs[n], O_RDONLY, O_DIRECTORY)) < 0) {
            continue;
        }
        fwfd = openat(fd, path, O_RDONLY);
        close(fd);
        if (fwfd >= 0) {
            *size = lseek(fwfd, 0, SEEK_END);
            zx_status_t r = fdio_get_vmo_clone(fwfd, vmo);
            close(fwfd);
            return r;
        }
        if (errno != ENOENT) {
            return ZX_ERR_IO;
        }
    }
    return ZX_ERR_NOT_FOUND;
}

static zx_status_t dc_get_metadata(device_t* dev, uint32_t type, void* buffer, size_t buflen,
                                   size_t* actual) {
    dc_metadata_t* md;

    // search dev and its parent devices for a match
    while (dev) {
        list_for_every_entry(&dev->metadata, md, dc_metadata_t, node) {
            if (md->type == type) {
                if (md->length > buflen) {
                    return ZX_ERR_BUFFER_TOO_SMALL;
                }
                memcpy(buffer, md->data, md->length);
                *actual = md->length;
                return ZX_OK;
            }
        }
        dev = dev->parent;
    }

    return ZX_ERR_NOT_FOUND;
}

static zx_status_t dc_add_metadata(device_t* dev, uint32_t type, const void* data,
                                   uint32_t length) {
    dc_metadata_t* md = calloc(1, sizeof(dc_metadata_t) + length);
    if (!md) {
        return ZX_ERR_NO_MEMORY;
    }

    md->type = type;
    md->length = length;
    memcpy(&md->data, data, length);
    list_add_head(&dev->metadata, &md->node);
    return ZX_OK;
}

static zx_status_t dc_publish_metadata(device_t* dev, const char* path, uint32_t type,
                                       const void* data, uint32_t length) {
    if (!path || strncmp(path, "/dev/sys/", strlen("/dev/sys/"))) {
        return ZX_ERR_INVALID_ARGS;
    }

    // TODO: this should probably be restricted to the root devhost

    dc_metadata_t* md = calloc(1, sizeof(dc_metadata_t) + length + strlen(path) + 1);
    if (!md) {
        return ZX_ERR_NO_MEMORY;
    }

    md->type = type;
    md->length = length;
    md->has_path = true;
    memcpy(&md->data, data, length);
    strcpy((char*)md->data + length, path);
    list_add_head(&published_metadata, &md->node);
    return ZX_OK;
}

static zx_status_t dc_handle_device_read(device_t* dev) {
    dc_msg_t msg;
    zx_handle_t hin[3];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 3;

    if (dev->flags & DEV_CTX_DEAD) {
        log(ERROR, "devcoord: dev %p already dead (in read)\n", dev);
        return ZX_ERR_INTERNAL;
    }

    zx_status_t r;
    if ((r = zx_channel_read(dev->hrpc, 0, &msg, hin,
                             msize, hcount, &msize, &hcount)) < 0) {
        return r;
    }

    const void* data;
    const char* name;
    const char* args;
    if ((r = dc_msg_unpack(&msg, msize, &data, &name, &args)) < 0) {
        while (hcount > 0) {
            zx_handle_close(hin[--hcount]);
        }
        return ZX_ERR_INTERNAL;
    }

    dc_status_t dcs;
    dcs.txid = msg.txid;

    switch (msg.op) {
    case DC_OP_ADD_DEVICE:
    case DC_OP_ADD_DEVICE_INVISIBLE:
        if (hcount != 1) {
            goto fail_wrong_hcount;
        }
        if (dc_in_suspend()) {
            log(ERROR, "devcoord: rpc: add-device '%s' forbidden in suspend\n",
                name);
            r = ZX_ERR_BAD_STATE;
            goto fail_close_handles;
        }
        log(RPC_IN, "devcoord: rpc: add-device '%s' args='%s'\n", name, args);
        if ((r = dc_add_device(dev, hin[0], &msg, name, args, data,
                               msg.op == DC_OP_ADD_DEVICE_INVISIBLE)) < 0) {
            zx_handle_close(hin[0]);
        }
        break;

    case DC_OP_REMOVE_DEVICE:
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        if (dc_in_suspend()) {
            log(ERROR, "devcoord: rpc: remove-device '%s' forbidden in suspend\n",
                dev->name);
            r = ZX_ERR_BAD_STATE;
            goto fail_close_handles;
        }
        log(RPC_IN, "devcoord: rpc: remove-device '%s'\n", dev->name);
        dc_remove_device(dev, false);
        goto disconnect;

    case DC_OP_MAKE_VISIBLE:
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        if (dc_in_suspend()) {
            log(ERROR, "devcoord: rpc: make-visible '%s' forbidden in suspend\n",
                dev->name);
            r = ZX_ERR_BAD_STATE;
            goto fail_close_handles;
        }
        log(RPC_IN, "devcoord: rpc: make-visible '%s'\n", dev->name);
        dc_make_visible(dev);
        r = ZX_OK;
        break;

    case DC_OP_BIND_DEVICE:
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        if (dc_in_suspend()) {
            log(ERROR, "devcoord: rpc: bind-device '%s' forbidden in suspend\n",
                dev->name);
            r = ZX_ERR_BAD_STATE;
            goto fail_close_handles;
        }
        log(RPC_IN, "devcoord: rpc: bind-device '%s'\n", dev->name);
        r = dc_bind_device(dev, args);
        break;

    case DC_OP_DM_COMMAND:
        if (hcount > 1) {
            goto fail_wrong_hcount;
        }
        if (dc_in_suspend()) {
            log(ERROR, "devcoord: rpc: dm-command forbidden in suspend\n");
            r = ZX_ERR_BAD_STATE;
            goto fail_close_handles;
        }
        if (hcount == 1) {
            dmctl_socket = hin[0];
        }
        r = handle_dmctl_write(msg.datalen, data);
        if (dmctl_socket != ZX_HANDLE_INVALID) {
            zx_handle_close(dmctl_socket);
            dmctl_socket = ZX_HANDLE_INVALID;
        }
        break;

    case DC_OP_DM_OPEN_VIRTCON:
        if (hcount != 1) {
            goto fail_wrong_hcount;
        }
        zx_channel_write(virtcon_open, 0, NULL, 0, hin, 1);
        r = ZX_OK;
        break;

    case DC_OP_DM_WATCH:
        if (hcount != 1) {
            goto fail_wrong_hcount;
        }
        dc_watch(hin[0]);
        r = ZX_OK;
        break;

    case DC_OP_DM_MEXEC:
        if (hcount != 2) {
            log(ERROR, "devcoord: rpc: mexec wrong hcount %d\n", hcount);
            goto fail_wrong_hcount;
        }
        dc_mexec(hin);
        r = ZX_OK;
        break;

    case DC_OP_GET_TOPO_PATH: {
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        struct {
            dc_status_t rsp;
            char path[DC_PATH_MAX];
        } reply;
        if ((r = dc_get_topo_path(dev, reply.path, DC_PATH_MAX)) < 0) {
            break;
        }
        reply.rsp.status = ZX_OK;
        reply.rsp.txid = msg.txid;
        if ((r = zx_channel_write(dev->hrpc, 0, &reply, sizeof(reply), NULL, 0)) < 0) {
            return r;
        }
        return ZX_OK;
    }
    case DC_OP_LOAD_FIRMWARE: {
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        zx_handle_t vmo;
        struct {
            dc_status_t rsp;
            size_t size;
        } reply;
        if ((r = dc_load_firmware(dev, args, &vmo, &reply.size)) < 0) {
            break;
        }
        reply.rsp.status = ZX_OK;
        reply.rsp.txid = msg.txid;
        if ((r = zx_channel_write(dev->hrpc, 0, &reply, sizeof(reply), &vmo, 1)) < 0) {
            return r;
        }
        return ZX_OK;
    }
    case DC_OP_STATUS: {
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        // all of these return directly and do not write a
        // reply, since this message is a reply itself
        pending_t* pending = list_remove_head_type(&dev->pending, pending_t, node);
        if (pending == NULL) {
            log(ERROR, "devcoord: rpc: spurious status message\n");
            return ZX_OK;
        }
        switch (pending->op) {
        case PENDING_BIND:
            if (msg.status != ZX_OK) {
                log(ERROR, "devcoord: rpc: bind-driver '%s' status %d\n",
                    dev->name, msg.status);
            } else {
                dc_notify(dev, DEVMGR_OP_DEVICE_CHANGED);
            }
            //TODO: try next driver, clear BOUND flag
            break;
        case PENDING_SUSPEND: {
            if (msg.status != ZX_OK) {
                log(ERROR, "devcoord: rpc: suspend '%s' status %d\n",
                    dev->name, msg.status);
            }
            suspend_context_t* ctx = pending->ctx;
            ctx->status = msg.status;
            dc_continue_suspend((suspend_context_t*)pending->ctx);
            break;
        }
        }
        free(pending);
        return ZX_OK;
    }
    case DC_OP_GET_METADATA: {
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        struct {
            dc_status_t rsp;
            uint8_t data[DC_MAX_DATA];
        } reply;
        size_t actual = 0;
        reply.rsp.status = dc_get_metadata(dev, msg.value, &reply.data, sizeof(reply.data),
                                           &actual);
        reply.rsp.txid = msg.txid;
        return zx_channel_write(dev->hrpc, 0, &reply, sizeof(reply.rsp) + actual, NULL, 0);
    }
    case DC_OP_ADD_METADATA: {
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        r = dc_add_metadata(dev, msg.value, data, msg.datalen);
        break;
    }
    case DC_OP_PUBLISH_METADATA: {
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        r = dc_publish_metadata(dev, args, msg.value, data, msg.datalen);
        break;
    }
    default:
        log(ERROR, "devcoord: invalid rpc op %08x\n", msg.op);
        r = ZX_ERR_NOT_SUPPORTED;
        goto fail_close_handles;
    }

done:
    dcs.status = r;
    if ((r = zx_channel_write(dev->hrpc, 0, &dcs, sizeof(dcs), NULL, 0)) < 0) {
        return r;
    }
    return ZX_OK;

disconnect:
    dcs.status = ZX_OK;
    zx_channel_write(dev->hrpc, 0, &dcs, sizeof(dcs), NULL, 0);
    return ZX_ERR_STOP;

fail_wrong_hcount:
    r = ZX_ERR_INVALID_ARGS;
fail_close_handles:
    while (hcount > 0) {
        zx_handle_close(hin[--hcount]);
    }
    goto done;
}

#define dev_from_ph(ph) containerof(ph, device_t, ph)

// handle inbound RPCs from devhost to devices
static zx_status_t dc_handle_device(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    device_t* dev = dev_from_ph(ph);

    if (signals & ZX_CHANNEL_READABLE) {
        zx_status_t r;
        if ((r = dc_handle_device_read(dev)) < 0) {
            if (r != ZX_ERR_STOP) {
                log(ERROR, "devcoord: device %p name='%s' rpc status: %d\n",
                    dev, dev->name, r);
            }
            dc_remove_device(dev, true);
            return ZX_ERR_STOP;
        }
        return ZX_OK;
    }
    if (signals & ZX_CHANNEL_PEER_CLOSED) {
        log(ERROR, "devcoord: device %p name='%s' disconnected!\n", dev, dev->name);
        dc_remove_device(dev, true);
        return ZX_ERR_STOP;
    }
    log(ERROR, "devcoord: no work? %08x\n", signals);
    return ZX_OK;
}

// send message to devhost, requesting the creation of a device
static zx_status_t dh_create_device(device_t* dev, devhost_t* dh,
                                    const char* args, zx_handle_t rpc_proxy) {
    dc_msg_t msg;
    uint32_t mlen;
    zx_status_t r;

    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, dev->libname, args)) < 0) {
        return r;
    }

    uint32_t hcount = 0;
    zx_handle_t handle[3], hrpc;
    if ((r = zx_channel_create(0, handle, &hrpc)) < 0) {
        return r;
    }
    hcount++;

    if (dev->libname[0]) {
        if ((r = libname_to_vmo(dev->libname, handle + 1)) < 0) {
            goto fail;
        }
        hcount++;
        msg.op = DC_OP_CREATE_DEVICE;
    } else {
        msg.op = DC_OP_CREATE_DEVICE_STUB;
    }

    if (rpc_proxy) {
        handle[hcount++] = rpc_proxy;
    }

    msg.txid = 0;
    msg.protocol_id = dev->protocol_id;

    if ((r = zx_channel_write(dh->hrpc, 0, &msg, mlen, handle, hcount)) < 0) {
        goto fail_after_write;
    }

    dev->hrpc = hrpc;
    dev->ph.handle = hrpc;
    dev->ph.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    dev->ph.func = dc_handle_device;
    if ((r = port_wait(&dc_port, &dev->ph)) < 0) {
        goto fail_after_write;
    }
    dev->host = dh;
    dh->refcount++;
    list_add_tail(&dh->devices, &dev->dhnode);
    return ZX_OK;

fail:
    zx_handle_close_many(handle, hcount);
fail_after_write:
    zx_handle_close(hrpc);
    return r;
}

static zx_status_t dc_create_proxy(device_t* parent) {
    if (parent->proxy != NULL) {
        return ZX_OK;
    }

    size_t namelen = strlen(parent->name);
    size_t liblen = strlen(parent->libname);
    size_t devlen = sizeof(device_t) + namelen + liblen + 2;

    // non-immortal devices, use foo.proxy.so for
    // their proxy devices instead of foo.so
    bool proxylib = !(parent->flags & DEV_CTX_IMMORTAL);

    if (proxylib) {
        if (liblen < 3) {
            return ZX_ERR_INTERNAL;
        }
        // space for ".proxy"
        devlen += 6;
    }

    device_t* dev = calloc(1, devlen);
    if (dev == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    char* text = (char*) (dev + 1);
    memcpy(text, parent->name, namelen + 1);
    dev->name = text;
    text += namelen + 1;
    memcpy(text, parent->libname, liblen + 1);
    if (proxylib) {
        memcpy(text + liblen - 3, ".proxy.so", 10);
    }
    dev->libname = text;

    list_initialize(&dev->children);
    list_initialize(&dev->pending);
    list_initialize(&dev->metadata);
    dev->flags = DEV_CTX_PROXY;
    dev->protocol_id = parent->protocol_id;
    dev->parent = parent;
    dev->refcount = 1;
    parent->proxy = dev;
    parent->refcount++;
    log(DEVLC, "devcoord: dev %p name='%s' ++ref=%d (proxy)\n",
        parent, parent->name, parent->refcount);
    return ZX_OK;
}

// send message to devhost, requesting the binding of a driver to a device
static zx_status_t dh_bind_driver(device_t* dev, const char* libname) {
    dc_msg_t msg;
    uint32_t mlen;

    pending_t* pending = malloc(sizeof(pending_t));
    if (pending == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t r;
    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, libname, NULL)) < 0) {
        free(pending);
        return r;
    }

    zx_handle_t vmo;
    if ((r = libname_to_vmo(libname, &vmo)) < 0) {
        free(pending);
        return r;
    }

    msg.txid = 0;
    msg.op = DC_OP_BIND_DRIVER;

    if ((r = zx_channel_write(dev->hrpc, 0, &msg, mlen, &vmo, 1)) < 0) {
        free(pending);
        return r;
    }

    dev->flags |= DEV_CTX_BOUND;
    pending->op = PENDING_BIND;
    pending->ctx = NULL;
    list_add_tail(&dev->pending, &pending->node);
    return ZX_OK;
}

static zx_status_t dh_connect_proxy(device_t* dev, zx_handle_t h) {
    dc_msg_t msg;
    uint32_t mlen;
    zx_status_t r;
    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, NULL, NULL)) < 0) {
        zx_handle_close(h);
        return r;
    }
    msg.txid = 0;
    msg.op = DC_OP_CONNECT_PROXY;
    return zx_channel_write(dev->hrpc, 0, &msg, mlen, &h, 1);
}

static zx_status_t dc_prepare_proxy(device_t* dev) {
    if (dev->flags & DEV_CTX_PROXY) {
        log(ERROR, "devcoord: cannot proxy a proxy: %s\n", dev->name);
        return ZX_ERR_INTERNAL;
    }

    // proxy args are "processname,args"
    const char* arg0 = dev->args;
    const char* arg1 = strchr(arg0, ',');
    if (arg1 == NULL) {
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
    if (dev->proxy->host == NULL) {
        zx_handle_t h0 = ZX_HANDLE_INVALID, h1 = ZX_HANDLE_INVALID;

        // the immortal root devices do not provide proxy rpc
        bool need_proxy_rpc = !(dev->flags & DEV_CTX_IMMORTAL);

        if (need_proxy_rpc) {
            // create rpc channel for proxy device to talk to the busdev it proxys
            if ((r = zx_channel_create(0, &h0, &h1)) < 0) {
                log(ERROR, "devcoord: cannot create proxy rpc channel: %d\n", r);
                return r;
            }
        } else if (dev == &sys_device) {
            // pass bootdata VMO handle to sys device
            h1 = bootdata_vmo;
        }
        if ((r = dc_new_devhost(devhostname, dev->host,
                                &dev->proxy->host)) < 0) {
            log(ERROR, "devcoord: dc_new_devhost: %d\n", r);
            zx_handle_close(h0);
            zx_handle_close(h1);
            return r;
        }
        if ((r = dh_create_device(dev->proxy, dev->proxy->host, arg1, h1)) < 0) {
            log(ERROR, "devcoord: dh_create_device: %d\n", r);
            zx_handle_close(h0);
            return r;
        }
        if (need_proxy_rpc) {
            if ((r = dh_connect_proxy(dev, h0)) < 0) {
                log(ERROR, "devcoord: dh_connect_proxy: %d\n", r);
            }
        }
    }

    return ZX_OK;
}

static zx_status_t dc_attempt_bind(driver_t* drv, device_t* dev) {
    // cannot bind driver to already bound device
    if ((dev->flags & DEV_CTX_BOUND) && (!(dev->flags & DEV_CTX_MULTI_BIND))) {
        return ZX_ERR_BAD_STATE;
    }
    if (!(dev->flags & DEV_CTX_MUST_ISOLATE)) {
        // non-busdev is pretty simple
        if (dev->host == NULL) {
            log(ERROR, "devcoord: can't bind to device without devhost\n");
            return ZX_ERR_BAD_STATE;
        }
        return dh_bind_driver(dev, drv->libname);
    }

    zx_status_t r;
    if ((r = dc_prepare_proxy(dev)) < 0) {
        return r;
    }

    r = dh_bind_driver(dev->proxy, drv->libname);
    //TODO(swetland): arrange to mark us unbound when the proxy (or its devhost) goes away
    if ((r == ZX_OK) && !(dev->flags & DEV_CTX_MULTI_BIND)) {
        dev->flags |= DEV_CTX_BOUND;
    }
    return r;
}

static void dc_handle_new_device(device_t* dev) {
    driver_t* drv;

    char path[DC_PATH_MAX];
    if (dc_get_topo_path(dev, path, DC_PATH_MAX) == ZX_OK) {
        // check for metadata in published_metadata
        // move any matches to new device's metadata list
        dc_metadata_t* md;
        dc_metadata_t* temp;
        list_for_every_entry_safe(&published_metadata, md, temp, dc_metadata_t, node) {
            char* md_path = (char*)md->data + md->length;
            if (!strcmp(md_path, path)) {
                list_delete(&md->node);
                list_add_tail(&dev->metadata, &md->node);
            }
        }
    }

    list_for_every_entry(&list_drivers, drv, driver_t, node) {
        if (dc_is_bindable(drv, dev->protocol_id,
                           dev->props, dev->prop_count, true)) {
            log(SPEW, "devcoord: drv='%s' bindable to dev='%s'\n",
                drv->name, dev->name);

            dc_attempt_bind(drv, dev);
            if (!(dev->flags & DEV_CTX_MULTI_BIND)) {
                break;
            }
        }
    }
}

static void dc_suspend_fallback(uint32_t flags) {
    log(INFO, "devcoord: suspend fallback with flags 0x%08x\n", flags);
    if (flags == DEVICE_SUSPEND_FLAG_REBOOT) {
        zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_REBOOT, NULL);
    } else if (flags == DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER) {
        zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_REBOOT_BOOTLOADER, NULL);
    } else if (flags == DEVICE_SUSPEND_FLAG_REBOOT_RECOVERY) {
        zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_REBOOT_RECOVERY, NULL);
    } else if (flags == DEVICE_SUSPEND_FLAG_POWEROFF) {
        zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_SHUTDOWN, NULL);
    }
}

static zx_status_t dc_suspend_devhost(devhost_t* dh, suspend_context_t* ctx) {
    device_t* dev = list_peek_head_type(&dh->devices, device_t, dhnode);
    if (!dev) {
        return ZX_OK;
    }

    if (!(dev->flags & DEV_CTX_PROXY)) {
        log(INFO, "devcoord: devhost root '%s' (%p) is not a proxy\n",
            dev->name, dev);
        return ZX_ERR_BAD_STATE;
    }

    log(DEVLC, "devcoord: suspend devhost %p device '%s' (%p)\n",
        dh, dev->name, dev);

    zx_handle_t rpc = ZX_HANDLE_INVALID;

    pending_t* pending = malloc(sizeof(pending_t));
    if (pending == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    dc_msg_t msg;
    uint32_t mlen;
    zx_status_t r;
    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, NULL, NULL)) < 0) {
        free(pending);
        return r;
    }
    msg.txid = 0;
    msg.op = DC_OP_SUSPEND;
    msg.value = ctx->sflags;
    rpc = dev->hrpc;
    if ((r = zx_channel_write(rpc, 0, &msg, mlen, NULL, 0)) != ZX_OK) {
        free(pending);
        return r;
    }

    dh->flags |= DEV_HOST_SUSPEND;
    pending->op = PENDING_SUSPEND;
    pending->ctx = ctx;
    list_add_tail(&dev->pending, &pending->node);

    ctx->count += 1;

    return ZX_OK;
}

static void append_suspend_list(suspend_context_t* ctx, devhost_t* dh) {
    // suspend order is children first
    devhost_t* child = NULL;
    list_for_every_entry(&dh->children, child, devhost_t, node) {
        list_add_head(&ctx->devhosts, &child->snode);
    }
    list_for_every_entry(&dh->children, child, devhost_t, node) {
        append_suspend_list(ctx, child);
    }
}

static void build_suspend_list(suspend_context_t* ctx) {
    // sys_device must suspend last as on x86 it invokes
    // ACPI S-state transition
    list_add_head(&ctx->devhosts, &sys_device.proxy->host->snode);
    append_suspend_list(ctx, sys_device.proxy->host);
    list_add_head(&ctx->devhosts, &root_device.proxy->host->snode);
    append_suspend_list(ctx, root_device.proxy->host);
    list_add_head(&ctx->devhosts, &misc_device.proxy->host->snode);
    append_suspend_list(ctx, misc_device.proxy->host);
    // test devices do not (yet) participate in suspend
}

static void process_suspend_list(suspend_context_t* ctx) {
    devhost_t* dh = ctx->dh;
    devhost_t* parent = NULL;
    do {
        if (!parent || (dh->parent == parent)) {
            // send DC_OP_SUSPEND each set of children of a devhost at a time,
            // since they can run in parallel
            dc_suspend_devhost(dh, &suspend_ctx);
            parent = dh->parent;
        } else {
            // if the parent is different than the previous devhost's
            // parent, either this devhost is the parent, a child of
            // its parent's sibling, or the parent's sibling, so stop
            // processing until all the outstanding suspends are done
            parent = NULL;
            break;
        }
    } while ((dh = list_next_type(&ctx->devhosts, &dh->snode,
                                  devhost_t, snode)) != NULL);
    // next devhost to process once all the outstanding suspends are done
    ctx->dh = dh;
}

static bool check_pending(device_t* dev) {
    pending_t* pending;
    if (dev->proxy) {
        pending = list_peek_tail_type(&dev->proxy->pending, pending_t, node);
    } else {
        pending = list_peek_tail_type(&dev->pending, pending_t, node);
    }
    if ((pending == NULL) || (pending->op != PENDING_SUSPEND)) {
        return false;
    } else {
        log(ERROR, "  devhost with device '%s' timed out\n", dev->name);
        return true;
    }
}

static int suspend_timeout_thread(void* arg) {
    // 10 seconds
    zx_nanosleep(zx_deadline_after(ZX_SEC(10)));

    suspend_context_t* ctx = arg;
    if (suspend_debug) {
        if (ctx->flags == RUNNING) {
            return 0; // success
        }
        log(ERROR, "devcoord: suspend time out\n");
        log(ERROR, "  sflags: 0x%08x\n", ctx->sflags);
        device_t* dev;
        list_for_every_entry(&list_devices, dev, device_t, anode) {
            check_pending(dev);
        }
        check_pending(&root_device);
        check_pending(&misc_device);
        check_pending(&sys_device);
    }
    if (suspend_fallback) {
        dc_suspend_fallback(ctx->sflags);
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

    suspend_context_t* ctx = &suspend_ctx;
    if (ctx->flags) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->status = ZX_OK;
    ctx->flags = SUSPEND;
    ctx->sflags = flags;
    ctx->socket = dmctl_socket;
    dmctl_socket = ZX_HANDLE_INVALID;   // to prevent the rpc handler from closing this handle
    list_initialize(&ctx->devhosts);

    build_suspend_list(ctx);

    if (suspend_fallback || suspend_debug) {
        thrd_t t;
        int ret = thrd_create_with_name(&t, suspend_timeout_thread, ctx,
                                        "devcoord-suspend-timeout");
        if (ret != thrd_success) {
            log(ERROR, "devcoord: can't create suspend timeout thread\n");
        }
    }

    ctx->dh = list_peek_head_type(&ctx->devhosts, devhost_t, snode);
    process_suspend_list(ctx);
}

static void dc_mexec(zx_handle_t* h) {
    // these top level devices should all have proxies. if not,
    // the system hasn't fully initialized yet and cannot mexec.
    if (!sys_device.proxy || !root_device.proxy || !misc_device.proxy) {
        return;
    }

    suspend_context_t* ctx = &suspend_ctx;
    if (ctx->flags) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->status = ZX_OK;
    ctx->flags = SUSPEND;
    ctx->sflags = DEVICE_SUSPEND_FLAG_MEXEC;
    list_initialize(&ctx->devhosts);

    ctx->kernel = *h;
    ctx->bootdata = *(h + 1);

    build_suspend_list(ctx);

    ctx->dh = list_peek_head_type(&ctx->devhosts, devhost_t, snode);
    process_suspend_list(ctx);
}

static void dc_continue_suspend(suspend_context_t* ctx) {
    if (ctx->status != ZX_OK) {
        // TODO: unroll suspend
        // do not continue to suspend as this indicates a driver suspend
        // problem and should show as a bug
        log(ERROR, "devcoord: failed to suspend\n");
        // notify dmctl
        if (ctx->socket) {
            zx_handle_close(ctx->socket);
        }
        if (ctx->sflags == DEVICE_SUSPEND_FLAG_MEXEC) {
            zx_object_signal(ctx->kernel, 0, ZX_USER_SIGNAL_0);
        }
        ctx->flags = 0;
        return;
    }

    ctx->count -= 1;
    if (ctx->count == 0) {
        if (ctx->dh != NULL) {
            process_suspend_list(ctx);
        } else if (ctx->sflags == DEVICE_SUSPEND_FLAG_MEXEC) {
            zx_system_mexec(get_root_resource(), ctx->kernel, ctx->bootdata);
        } else {
            // should never get here on x86
            // on arm, if the platform driver does not implement
            // suspend go to the kernel fallback
            dc_suspend_fallback(ctx->sflags);
            // this handle is leaked on the shutdown path for x86
            if (ctx->socket) {
                zx_handle_close(ctx->socket);
            }
            // if we get here the system did not suspend successfully
            ctx->flags = RUNNING;
        }
    }
}

// device binding program that pure (parentless)
// misc devices use to get published in the misc devhost
static struct zx_bind_inst misc_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT);

static bool is_misc_driver(driver_t* drv) {
    return (drv->binding_size == sizeof(misc_device_binding)) &&
        (memcmp(&misc_device_binding, drv->binding, sizeof(misc_device_binding)) == 0);
}

// device binding program that pure (parentless)
// test devices use to get published in the test devhost
static struct zx_bind_inst test_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT);

static bool is_test_driver(driver_t* drv) {
    return (drv->binding_size == sizeof(test_device_binding)) &&
        (memcmp(&test_device_binding, drv->binding, sizeof(test_device_binding)) == 0);
}


// device binding program that special root-level
// devices use to get published in the root devhost
static struct zx_bind_inst root_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_ROOT);

static bool is_root_driver(driver_t* drv) {
    return (drv->binding_size == sizeof(root_device_binding)) &&
        (memcmp(&root_device_binding, drv->binding, sizeof(root_device_binding)) == 0);
}

// dc_driver_added_init is called from driver enumeration during
// startup and before the devcoordinator starts running.  Enumerated
// drivers are added directly to the all-drivers or fallback list.
//
// TODO: fancier priorities
static void dc_driver_added_init(driver_t* drv, const char* version) {
    if (version[0] == '*') {
        // fallback driver, load only if all else fails
        list_add_tail(&list_drivers_fallback, &drv->node);
    } else if (version[0] == '!') {
        // debugging / development hack
        // prioritize drivers with version "!..." over others
        list_add_head(&list_drivers, &drv->node);
    } else {
        list_add_tail(&list_drivers, &drv->node);
    }
}

static work_t new_driver_work;

// dc_driver_added is called when a driver is added after the
// devcoordinator has started.  The driver is added to the new-drivers
// list and work is queued to process it.
static void dc_driver_added(driver_t* drv, const char* version) {
    list_add_tail(&list_drivers_new, &drv->node);
    if (new_driver_work.op == WORK_IDLE) {
        queue_work(&new_driver_work, WORK_DRIVER_ADDED, 0);
    }
}

device_t* coordinator_init(zx_handle_t root_job) {
    printf("coordinator_init()\n");

    zx_status_t status = zx_job_create(root_job, 0u, &devhost_job);
    if (status < 0) {
        log(ERROR, "devcoord: unable to create devhost job\n");
    }
    static const zx_policy_basic_t policy[] = {
        { ZX_POL_BAD_HANDLE, ZX_POL_ACTION_EXCEPTION },
    };
    status = zx_job_set_policy(devhost_job, ZX_JOB_POL_RELATIVE,
                               ZX_JOB_POL_BASIC, &policy, countof(policy));
    if (status < 0) {
        log(ERROR, "devcoord: zx_job_set_policy() failed\n");
    }
    zx_object_set_property(devhost_job, ZX_PROP_NAME, "zircon-drivers", 15);

    port_init(&dc_port);

    return &root_device;
}

// dc_bind_driver is called when a new driver becomes available to
// the devcoordinator.  Existing devices are inspected to see if the
// new driver is bindable to them (unless they are already bound).
void dc_bind_driver(driver_t* drv) {
    if (dc_running) {
        printf("devcoord: driver '%s' added\n", drv->name);
    }
    if (is_root_driver(drv)) {
        dc_attempt_bind(drv, &root_device);
    } else if (is_misc_driver(drv)) {
        dc_attempt_bind(drv, &misc_device);
    } else if (is_test_driver(drv)) {
        dc_attempt_bind(drv, &test_device);
    } else if (dc_running) {
        device_t* dev;
        list_for_every_entry(&list_devices, dev, device_t, anode) {
            if (dev->flags & (DEV_CTX_BOUND | DEV_CTX_DEAD |
                              DEV_CTX_ZOMBIE | DEV_CTX_INVISIBLE)) {
                // if device is already bound or being destroyed or invisible, skip it
                continue;
            }
            if (dc_is_bindable(drv, dev->protocol_id,
                               dev->props, dev->prop_count, true)) {
                log(INFO, "devcoord: drv='%s' bindable to dev='%s'\n",
                    drv->name, dev->name);

                dc_attempt_bind(drv, dev);
            }
        }
    }
}

void dc_handle_new_driver(void) {
    driver_t* drv;
    while ((drv = list_remove_head_type(&list_drivers_new, driver_t, node)) != NULL) {
        list_add_tail(&list_drivers, &drv->node);
        dc_bind_driver(drv);
    }
}

#define CTL_SCAN_SYSTEM 1
#define CTL_ADD_SYSTEM 2

static bool system_available;
static bool system_loaded;

// List of drivers loaded from /system by system_driver_loader()
static list_node_t list_drivers_system = LIST_INITIAL_VALUE(list_drivers_system);

static int system_driver_loader(void* arg);

static zx_status_t dc_control_event(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    switch (evt) {
    case CTL_SCAN_SYSTEM:
        if (!system_loaded) {
            system_loaded = true;
            // Fire up a thread to scan/load system drivers
            // This avoids deadlocks between the devhosts hosting the block devices
            // that these drivers may be served from and the devcoordinator loading them.
            thrd_t t;
            thrd_create_with_name(&t, system_driver_loader, NULL, "system-driver-loader");
        }
        break;
    case CTL_ADD_SYSTEM: {
        driver_t* drv;
        // Add system drivers to the new list
        while ((drv = list_remove_head_type(&list_drivers_system, driver_t, node)) != NULL) {
            list_add_tail(&list_drivers_new, &drv->node);
        }
        // Add any remaining fallback drivers to the new list
        while ((drv = list_remove_tail_type(&list_drivers_fallback, driver_t, node)) != NULL) {
            printf("devcoord: fallback driver '%s' is available\n", drv->name);
            list_add_tail(&list_drivers_new, &drv->node);
        }
        // Queue Driver Added work if not already queued
        if (new_driver_work.op == WORK_IDLE) {
            queue_work(&new_driver_work, WORK_DRIVER_ADDED, 0);
        }
        break;
    }
    }
    return ZX_OK;
}

static port_handler_t control_handler = {
    .func = dc_control_event,
};

// Drivers added during system scan (from the dedicated thread)
// are added to list_drivers_system for bulk processing once
// CTL_ADD_SYSTEM is sent.
//
// TODO: fancier priority management
static void dc_driver_added_sys(driver_t* drv, const char* version) {
    log(INFO, "devmgr: adding system driver '%s' '%s'\n", drv->name, drv->libname);

    if (load_vmo(drv->libname, &drv->dso_vmo)) {
        log(ERROR, "devmgr: system driver '%s' '%s' could not cache DSO\n", drv->name, drv->libname);
    }
    if (version[0] == '*') {
        // de-prioritize drivers that are "fallback"
        list_add_tail(&list_drivers_system, &drv->node);
    } else {
        list_add_head(&list_drivers_system, &drv->node);
    }
}

static int system_driver_loader(void* arg) {
    find_loadable_drivers("/system/driver", dc_driver_added_sys);
    find_loadable_drivers("/system/lib/driver", dc_driver_added_sys);
    port_queue(&dc_port, &control_handler, CTL_ADD_SYSTEM);
    return 0;
}

void load_system_drivers(void) {
    system_available = true;
    port_queue(&dc_port, &control_handler, CTL_SCAN_SYSTEM);
}

void coordinator(void) {
    log(INFO, "devmgr: coordinator()\n");

    if (getenv_bool("devmgr.verbose", false)) {
        log_flags |= LOG_DEVLC;
    }

    suspend_fallback = getenv_bool("devmgr.suspend-timeout-fallback", false);
    suspend_debug = getenv_bool("devmgr.suspend-timeout-debug", false);

    dc_asan_drivers = getenv_bool("devmgr.devhost.asan", false);

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
        dc_control_event(&control_handler, 0, CTL_SCAN_SYSTEM);
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
        driver_t* drv;
        while ((drv = list_remove_tail_type(&list_drivers_fallback, driver_t, node)) != NULL) {
            list_add_tail(&list_drivers, &drv->node);
        }
    }

    // Initial bind attempt for drivers enumerated at startup.
    driver_t* drv;
    list_for_every_entry(&list_drivers, drv, driver_t, node) {
        dc_bind_driver(drv);
    }

    dc_running = true;

    for (;;) {
        zx_status_t status;
        if (list_is_empty(&list_pending_work)) {
            status = port_dispatch(&dc_port, ZX_TIME_INFINITE, true);
        } else {
            status = port_dispatch(&dc_port, 0, true);
            if (status == ZX_ERR_TIMED_OUT) {
                process_work(list_remove_head_type(&list_pending_work, work_t, node));
                continue;
            }
        }
        if (status != ZX_OK) {
            log(ERROR, "devcoord: port dispatch ended: %d\n", status);
        }
    }
}
