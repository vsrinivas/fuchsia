// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ddk/driver.h>
#include <driver-info/driver-info.h>
#include <launchpad/launchpad.h>
#include <magenta/assert.h>
#include <magenta/ktrace.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/policy.h>
#include <magenta/device/dmctl.h>
#include <mxio/io.h>

#include "acpi.h"

#include "devcoordinator.h"
#include "devmgr.h"
#include "log.h"
#include "memfs-private.h"

extern mx_handle_t virtcon_open;

uint32_t log_flags = LOG_ERROR | LOG_INFO;

static void dc_dump_state(void);
static void dc_dump_devprops(void);
static void dc_dump_drivers(void);

static mx_handle_t dmctl_socket;

static void dmprintf(const char* fmt, ...) {
    if (dmctl_socket == MX_HANDLE_INVALID) {
        return;
    }
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t actual;
    if (mx_socket_write(dmctl_socket, 0, buf, strlen(buf), &actual) < 0) {
        mx_handle_close(dmctl_socket);
        dmctl_socket = MX_HANDLE_INVALID;
    }
}

static mx_status_t handle_dmctl_write(size_t len, const char* cmd) {
    if (len == 4) {
        if (!memcmp(cmd, "dump", 4)) {
            dc_dump_state();
            return MX_OK;
        }
        if (!memcmp(cmd, "help", 4)) {
            dmprintf("dump        - dump device tree\n"
                     "poweroff    - power off the system\n"
                     "shutdown    - power off the system\n"
                     "reboot      - reboot the system\n"
                     "kerneldebug - send a command to the kernel\n"
                     "ktraceoff   - stop kernel tracing\n"
                     "ktraceon    - start kernel tracing\n"
                     "devprops    - dump published devices and their binding properties\n"
                     "drivers     - list discovered drivers and their properties\n"
                     );
            return MX_OK;
        }
    }
    if ((len == 6) && !memcmp(cmd, "reboot", 6)) {
        devmgr_vfs_exit();
        devhost_acpi_reboot();
        return MX_OK;
    }
    if ((len == 7) && !memcmp(cmd, "drivers", 7)) {
        dc_dump_drivers();
        return MX_OK;
    }
    if (len == 8) {
        if (!memcmp(cmd, "poweroff", 8) || !memcmp(cmd, "shutdown", 8)) {
            devmgr_vfs_exit();
            devhost_acpi_poweroff();
            return MX_OK;
        }
        if (!memcmp(cmd, "ktraceon", 8)) {
            mx_ktrace_control(get_root_resource(), KTRACE_ACTION_START, KTRACE_GRP_ALL, NULL);
            return MX_OK;
        }
        if (!memcmp(cmd, "devprops", 8)) {
            dc_dump_devprops();
            return MX_OK;
        }
    }
    if ((len == 9) && (!memcmp(cmd, "ktraceoff", 9))) {
        mx_ktrace_control(get_root_resource(), KTRACE_ACTION_STOP, 0, NULL);
        mx_ktrace_control(get_root_resource(), KTRACE_ACTION_REWIND, 0, NULL);
        return MX_OK;
    }
    if ((len > 12) && !memcmp(cmd, "kerneldebug ", 12)) {
        return mx_debug_send_command(get_root_resource(), cmd + 12, len - 12);
    }
    if ((len > 11) && !memcmp(cmd, "add-driver:", 11)) {
        len -= 11;
        char path[len + 1];
        memcpy(path, cmd + 11, len);
        path[len] = 0;
        load_driver(path);
        return MX_OK;
    }
    dmprintf("unknown command\n");
    log(ERROR, "dmctl: unknown command '%.*s'\n", (int) len, cmd);
    return MX_ERR_NOT_SUPPORTED;
}

//TODO: these are copied from devhost.h
#define ID_HJOBROOT 4
mx_handle_t get_sysinfo_job_root(void);


static mx_status_t dc_handle_device(port_handler_t* ph, mx_signals_t signals, uint32_t evt);
static mx_status_t dc_attempt_bind(driver_t* drv, device_t* dev);

static bool dc_running;

static mx_handle_t dc_watch_channel;

static mx_handle_t devhost_job;
port_t dc_port;

// All Drivers
static list_node_t list_drivers = LIST_INITIAL_VALUE(list_drivers);

// Drivers to add to All Drivers
static list_node_t list_drivers_new = LIST_INITIAL_VALUE(list_drivers_new);

// All Devices (excluding static immortal devices)
static list_node_t list_devices = LIST_INITIAL_VALUE(list_devices);

static driver_t* libname_to_driver(const char* libname) {
    driver_t* drv;
    list_for_every_entry(&list_drivers, drv, driver_t, node) {
        if (!strcmp(libname, drv->libname)) {
            return drv;
        }
    }
    return NULL;
}

static mx_status_t libname_to_vmo(const char* libname, mx_handle_t* out) {
    driver_t* drv = libname_to_driver(libname);
    if (drv == NULL) {
        log(ERROR, "devcoord: cannot find driver '%s'\n", libname);
        return MX_ERR_NOT_FOUND;
    }
    int fd = open(libname, O_RDONLY);
    if (fd < 0) {
        log(ERROR, "devcoord: cannot open driver '%s'\n", libname);
        return MX_ERR_IO;
    }
    mx_status_t r = mxio_get_vmo(fd, out);
    close(fd);
    if (r < 0) {
        log(ERROR, "devcoord: cannot get driver vmo '%s'\n", libname);
    }
    return r;
}

static device_t root_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV | DEV_CTX_MULTI_BIND,
    .protocol_id = MX_PROTOCOL_ROOT,
    .name = "root",
    .libname = "",
    .args = "root,,",
    .children = LIST_INITIAL_VALUE(root_device.children),
    .pending = LIST_INITIAL_VALUE(root_device.pending),
    .refcount = 1,
};

static device_t misc_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV | DEV_CTX_MULTI_BIND,
    .protocol_id = MX_PROTOCOL_MISC_PARENT,
    .name = "misc",
    .libname = "",
    .args = "misc,,",
    .children = LIST_INITIAL_VALUE(misc_device.children),
    .pending = LIST_INITIAL_VALUE(misc_device.pending),
    .refcount = 1,
};

static mx_handle_t acpi_rpc[2] = { MX_HANDLE_INVALID, MX_HANDLE_INVALID };

static device_t acpi_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV,
    .protocol_id = MX_PROTOCOL_ACPI_BUS,
    .name = "acpi",
    .libname = "",
    .args = "acpi,,",
    .children = LIST_INITIAL_VALUE(acpi_device.children),
    .pending = LIST_INITIAL_VALUE(acpi_device.pending),
    .refcount = 1,
};

static device_t platform_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV,
    .name = "platform",
    .libname = "",
    .args = "platform,,",
    .children = LIST_INITIAL_VALUE(platform_device.children),
    .pending = LIST_INITIAL_VALUE(platform_device.pending),
    .refcount = 1,
};

device_t socket_device = {
    .flags = DEV_CTX_IMMORTAL,
    .protocol_id = 0,
    .name = "socket",
    .libname = "",
    .args = "",
    .children = LIST_INITIAL_VALUE(socket_device.children),
    .pending = LIST_INITIAL_VALUE(socket_device.pending),
    .refcount = 1,
};

void devmgr_set_mdi(mx_handle_t mdi_handle) {
    // MDI VMO handle is passed via via the resource handle
    platform_device.hrsrc = mdi_handle;
}

static void dc_dump_device(device_t* dev, size_t indent) {
    mx_koid_t pid = dev->host ? dev->host->koid : 0;
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
                 dev->flags & DEV_CTX_SHADOW ? '<' : '[',
                 dev->name,
                 dev->flags & DEV_CTX_SHADOW ? '>' : ']',
                 pid, extra,
                 dev->libname ? dev->libname : "");
    }
    device_t* child;
    if (dev->shadow) {
        indent++;
        dc_dump_device(dev->shadow, indent);
    }
    list_for_every_entry(&dev->children, child, device_t, node) {
        dc_dump_device(child, indent + 1);
    }
}

static void dc_dump_state(void) {
    dc_dump_device(&root_device, 0);
    dc_dump_device(&misc_device, 1);
    dc_dump_device(&acpi_device, 1);
    if (platform_device.hrsrc != MX_HANDLE_INVALID) {
        dc_dump_device(&platform_device, 1);
    }
}

static void dc_dump_device_props(device_t* dev) {
    if (dev->host) {
        dmprintf("Name [%s]%s%s%s\n",
                 dev->name,
                 dev->libname ? " Driver [" : "",
                 dev->libname ? dev->libname : "",
                 dev->libname ? "]" : "");
        dmprintf("Flags   :%s%s%s%s%s%s%s\n",
                 dev->flags & DEV_CTX_IMMORTAL   ? " Immortal"  : "",
                 dev->flags & DEV_CTX_BUSDEV     ? " BusDev"    : "",
                 dev->flags & DEV_CTX_MULTI_BIND ? " MultiBind" : "",
                 dev->flags & DEV_CTX_BOUND      ? " Bound"     : "",
                 dev->flags & DEV_CTX_DEAD       ? " Dead"      : "",
                 dev->flags & DEV_CTX_ZOMBIE     ? " Zombie"    : "",
                 dev->flags & DEV_CTX_SHADOW     ? " Shadow"    : "");

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
            const mx_device_prop_t* p = dev->props + i;
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
    if (dev->shadow) {
        dc_dump_device_props(dev->shadow);
    }
    list_for_every_entry(&dev->children, child, device_t, node) {
        dc_dump_device_props(child);
    }
}

static void dc_dump_devprops(void) {
    dc_dump_device_props(&root_device);
    dc_dump_device_props(&misc_device);
    dc_dump_device_props(&acpi_device);
    if (platform_device.hrsrc != MX_HANDLE_INVALID) {
        dc_dump_device_props(&platform_device);
    }
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
    MX_ASSERT(work->op == WORK_IDLE);
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

static const char* devhost_bin = "/boot/bin/devhost";

mx_handle_t get_service_root(void);

static mx_status_t dc_get_topo_path(device_t* dev, char* out, size_t max) {
    char tmp[max];
    char* path = tmp + max - 1;
    *path = 0;
    size_t total = 1;

    while (dev != NULL) {
        if (dev->flags & DEV_CTX_SHADOW) {
            dev = dev->parent;
        }
        const char* name;

        if (dev->parent) {
            name = dev->name;
        } else if (!strcmp(misc_device.name, dev->name)) {
            name = "dev/misc";
        } else if (!strcmp(acpi_device.name, dev->name)) {
            name = "dev/acpi";
        } else {
            name = "dev";
        }
        size_t len = strlen(name) + 1;
        if (len > (max - total)) {
            return MX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(path - len + 1, name, len - 1);
        path -= len;
        *path = '/';
        total += len;
        dev = dev->parent;
    }

    memcpy(out, path, total);
    return MX_OK;
}

//TODO: use a better device identifier
static mx_status_t dc_notify(device_t* dev, uint32_t op) {
    if (dc_watch_channel == MX_HANDLE_INVALID) {
        return MX_ERR_BAD_STATE;
    }
    mx_status_t r;
    if (op == DEVMGR_OP_DEVICE_ADDED) {
        size_t propslen = sizeof(mx_device_prop_t) * dev->prop_count;
        size_t len = sizeof(devmgr_event_t) + propslen;
        char msg[len + DC_PATH_MAX];
        devmgr_event_t* evt = (void*) msg;
        memset(evt, 0, sizeof(devmgr_event_t));
        memcpy(msg + sizeof(devmgr_event_t), dev->props, propslen);
        if (dc_get_topo_path(dev, msg + len, DC_PATH_MAX) < 0) {
            return MX_OK;
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
        r = mx_channel_write(dc_watch_channel, 0, msg, len, NULL, 0);
    } else {
        devmgr_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.opcode = op;
        if (dev->flags & DEV_CTX_BOUND) {
            evt.flags |= DEVMGR_FLAGS_BOUND;
        }
        evt.id = (uintptr_t) dev;
        r = mx_channel_write(dc_watch_channel, 0, &evt, sizeof(evt), NULL, 0);
    }
    if (r < 0) {
        mx_handle_close(dc_watch_channel);
        dc_watch_channel = MX_HANDLE_INVALID;
    }
    return r;
}

static void dc_watch(mx_handle_t h) {
    if (dc_watch_channel != MX_HANDLE_INVALID) {
        mx_handle_close(dc_watch_channel);
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

static mx_status_t dc_launch_devhost(devhost_t* host,
                                     const char* name, mx_handle_t hrpc) {
    launchpad_t* lp;
    launchpad_create_with_jobs(devhost_job, 0, name, &lp);
    launchpad_load_from_file(lp, devhost_bin);
    launchpad_set_args(lp, 1, &devhost_bin);

    launchpad_add_handle(lp, hrpc, PA_HND(PA_USER0, 0));

    mx_handle_t h;
    //TODO: limit root resource to root devhost only
    mx_handle_duplicate(get_root_resource(), MX_RIGHT_SAME_RIGHTS, &h);
    launchpad_add_handle(lp, h, PA_HND(PA_RESOURCE, 0));

    // Inherit devmgr's environment (including kernel cmdline)
    launchpad_clone(lp, LP_CLONE_ENVIRON);

    const char* nametable[2] = { "/", "/svc", };
    size_t name_count = 0;

    //TODO: eventually devhosts should not have vfs access
    launchpad_add_handle(lp, vfs_create_global_root_handle(),
                         PA_HND(PA_NS_DIR, name_count++));

    //TODO: constrain to /svc/device
    if ((h = get_service_root()) != MX_HANDLE_INVALID) {
        launchpad_add_handle(lp, h, PA_HND(PA_NS_DIR, name_count++));
    }

    launchpad_set_nametable(lp, name_count, nametable);

    //TODO: limit root job access to root devhost only
    launchpad_add_handle(lp, get_sysinfo_job_root(),
                         PA_HND(PA_USER0, ID_HJOBROOT));

    //TODO: pass a channel to the acpi devhost to rpc with
    //      devcoordinator, so it can call reboot/poweroff/ps0.
    //      come up with a better way to wire this up.
    if (!strcmp(name, "devhost:acpi")) {
        launchpad_add_handle(lp, acpi_rpc[1], PA_HND(PA_USER0, 10));
    }

    const char* errmsg;
    mx_status_t status = launchpad_go(lp, &host->proc, &errmsg);
    if (status < 0) {
        log(ERROR, "devcoord: launch devhost '%s': failed: %d: %s\n",
            name, status, errmsg);
        return status;
    }
    mx_info_handle_basic_t info;
    if (mx_object_get_info(host->proc, MX_INFO_HANDLE_BASIC, &info,
                           sizeof(info), NULL, NULL) == MX_OK) {
        host->koid = info.koid;
    }
    log(INFO, "devcoord: launch devhost '%s': pid=%zu\n",
        name, host->koid);

    return MX_OK;
}

static mx_status_t dc_new_devhost(const char* name, devhost_t** out) {
    devhost_t* dh = calloc(1, sizeof(devhost_t));
    if (dh == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    mx_handle_t hrpc;
    mx_status_t r;
    if ((r = mx_channel_create(0, &hrpc, &dh->hrpc)) < 0) {
        free(dh);
        return r;
    }

    if ((r = dc_launch_devhost(dh, name, hrpc)) < 0) {
        mx_handle_close(dh->hrpc);
        free(dh);
        return r;
    }

    list_initialize(&dh->devices);

    *out = dh;
    return MX_OK;
}

static void dc_release_devhost(devhost_t* dh) {
    log(DEVLC, "devcoord: release host %p\n", dh);
    dh->refcount--;
    if (dh->refcount > 0) {
        return;
    }
    log(INFO, "devcoord: destroy host %p\n", dh);
    mx_handle_close(dh->hrpc);
    mx_task_kill(dh->proc);
    mx_handle_close(dh->proc);
    free(dh);
}

// called when device children or shadows are removed
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

    if (dev->hrpc != MX_HANDLE_INVALID) {
        mx_handle_close(dev->hrpc);
        dev->hrpc = MX_HANDLE_INVALID;
        dev->ph.handle = MX_HANDLE_INVALID;
    }
    if (dev->hrsrc != MX_HANDLE_INVALID) {
        mx_handle_close(dev->hrsrc);
        dev->hrsrc = MX_HANDLE_INVALID;
    }
    dev->host = NULL;

    cancel_work(&dev->work);

    //TODO: cancel any pending rpc responses
    free(dev);
}

// Add a new device to a parent device (same devhost)
// New device is published in devfs.
// Caller closes handles on error, so we don't have to.
static mx_status_t dc_add_device(device_t* parent,
                                 mx_handle_t* handle, size_t hcount,
                                 dc_msg_t* msg, const char* name,
                                 const char* args, const void* data) {
    if (hcount == 0) {
        return MX_ERR_INVALID_ARGS;
    }
    if (msg->datalen % sizeof(mx_device_prop_t)) {
        return MX_ERR_INVALID_ARGS;
    }
    device_t* dev;
    // allocate device struct, followed by space for props, followed
    // by space for bus arguments, followed by space for the name
    size_t sz = sizeof(*dev) + msg->datalen + msg->argslen + msg->namelen + 2;
    if ((dev = calloc(1, sz)) == NULL) {
        return MX_ERR_NO_MEMORY;
    }
    list_initialize(&dev->children);
    list_initialize(&dev->pending);
    dev->hrpc = handle[0];
    dev->hrsrc = (hcount > 1) ? handle[1] : MX_HANDLE_INVALID;
    dev->prop_count = msg->datalen / sizeof(mx_device_prop_t);
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

    if (strlen(dev->name) > MX_DEVICE_NAME_MAX) {
        free(dev);
        return MX_ERR_INVALID_ARGS;
    }

    // If we have bus device args or resource handle
    // we are, by definition a bus device.
    if (args[0] || (dev->hrsrc != MX_HANDLE_INVALID)) {
        dev->flags |= DEV_CTX_BUSDEV;
    }

    // We exist within our parent's device host
    dev->host = parent->host;

    // If our parent is a shadow, for the purpose
    // of devicefs, we need to work with *its* parent
    // which is the device that it is shadowing.
    if (parent->flags & DEV_CTX_SHADOW) {
        parent = parent->parent;
    }

    mx_status_t r;
    if ((r = devfs_publish(parent, dev)) < 0) {
        free(dev);
        return r;
    }

    dev->ph.handle = handle[0];
    dev->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
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
    dev->parent = parent;
    list_add_tail(&parent->children, &dev->node);
    parent->refcount++;

    list_add_tail(&list_devices, &dev->anode);

    log(DEVLC, "devcoord: dev %p name='%s' ++ref=%d (child)\n",
        parent, parent->name, parent->refcount);

    log(DEVLC, "devcoord: publish %p '%s' props=%u args='%s' parent=%p\n",
        dev, dev->name, dev->prop_count, dev->args, dev->parent);

    dc_notify(dev, DEVMGR_OP_DEVICE_ADDED);
    queue_work(&dev->work, WORK_DEVICE_ADDED, 0);
    return MX_OK;
}

// Remove device from parent
// forced indicates this is removal due to a channel close
// or process exit, which means we should remove all other
// devices that share the devhost at the same time
static mx_status_t dc_remove_device(device_t* dev, bool forced) {
    if (dev->flags & DEV_CTX_ZOMBIE) {
        // This device was removed due to its devhost dying
        // (process exit or some other channel on that devhost
        // closing), and is now receiving the final remove call
        dev->flags &= (~DEV_CTX_ZOMBIE);
        dc_release_device(dev);
        return MX_OK;
    }
    if (dev->flags & DEV_CTX_DEAD) {
        // This should not happen
        log(ERROR, "devcoord: cannot remove dev %p name='%s' twice!\n", dev, dev->name);
        return MX_ERR_BAD_STATE;
    }
    if (dev->flags & DEV_CTX_IMMORTAL) {
        // This too should not happen
        log(ERROR, "devcoord: cannot remove dev %p name='%s' (immortal)\n", dev, dev->name);
        return MX_ERR_BAD_STATE;
    }

    log(DEVLC, "devcoord: remove %p name='%s' parent=%p\n", dev, dev->name, dev->parent);
    dev->flags |= DEV_CTX_DEAD;

    // remove from devfs, preventing further OPEN attempts
    devfs_unpublish(dev);

    // detach from devhost
    devhost_t* dh = dev->host;
    if (dh != NULL) {
        dev->host = NULL;
        list_delete(&dev->dhnode);

        // If we are responding to a disconnect,
        // we'll remove all the other devices on this devhost too.
        // A side-effect of this is that the devhost will be released,
        // as well as any shadow devices.
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
        if (dev->flags & DEV_CTX_SHADOW) {
            parent->shadow = NULL;
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
                if (!(parent->flags & DEV_CTX_DEAD) && (parent->flags & DEV_CTX_BUSDEV) &&
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

    if (!(dev->flags & DEV_CTX_SHADOW)) {
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
    return MX_OK;
}

static mx_status_t dc_bind_device(device_t* dev, const char* drvlibname) {
     log(INFO, "devcoord: dc_bind_device() '%s'\n", drvlibname);

    // shouldn't be possible to get a bind request for a shadow device
    if (dev->flags & DEV_CTX_SHADOW) {
        return MX_ERR_NOT_SUPPORTED;
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
                break;
            }
        }
    }

    return MX_OK;
};

static mx_status_t dc_handle_device_read(device_t* dev) {
    dc_msg_t msg;
    mx_handle_t hin[2];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 2;

    if (dev->flags & DEV_CTX_DEAD) {
        log(ERROR, "devcoord: dev %p already dead (in read)\n", dev);
        return MX_ERR_INTERNAL;
    }

    mx_status_t r;
    if ((r = mx_channel_read(dev->hrpc, 0, &msg, hin,
                             msize, hcount, &msize, &hcount)) < 0) {
        return r;
    }

    const void* data;
    const char* name;
    const char* args;
    if ((r = dc_msg_unpack(&msg, msize, &data, &name, &args)) < 0) {
        while (hcount > 0) {
            mx_handle_close(hin[--hcount]);
        }
        return MX_ERR_INTERNAL;
    }

    dc_status_t dcs;
    dcs.txid = msg.txid;

    switch (msg.op) {
    case DC_OP_ADD_DEVICE:
        log(RPC_IN, "devcoord: rpc: add-device '%s' args='%s'\n", name, args);
        if ((r = dc_add_device(dev, hin, hcount, &msg, name, args, data)) < 0) {
            while (hcount > 0) {
                mx_handle_close(hin[--hcount]);
            }
        }
        break;

    case DC_OP_REMOVE_DEVICE:
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        log(RPC_IN, "devcoord: rpc: remove-device '%s'\n", dev->name);
        dc_remove_device(dev, false);
        goto disconnect;

    case DC_OP_BIND_DEVICE:
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        log(RPC_IN, "devcoord: rpc: bind-device '%s'\n", dev->name);
        r = dc_bind_device(dev, args);
        break;

    case DC_OP_DM_COMMAND:
        if (hcount > 1) {
            goto fail_wrong_hcount;
        }
        if (hcount == 1) {
            dmctl_socket = hin[0];
        }
        r = handle_dmctl_write(msg.datalen, data);
        if (dmctl_socket != MX_HANDLE_INVALID) {
            mx_handle_close(dmctl_socket);
            dmctl_socket = MX_HANDLE_INVALID;
        }
        break;

    case DC_OP_DM_OPEN_VIRTCON:
        if (hcount != 1) {
            goto fail_wrong_hcount;
        }
        if (mx_channel_write(virtcon_open, 0, NULL, 0, hin, 1) < 0) {
            mx_handle_close(hin[0]);
        }
        r = MX_OK;
        break;

    case DC_OP_DM_WATCH:
        if (hcount != 1) {
            goto fail_wrong_hcount;
        }
        dc_watch(hin[0]);
        r = MX_OK;
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
        reply.rsp.status = MX_OK;
        reply.rsp.txid = msg.txid;
        if ((r = mx_channel_write(dev->hrpc, 0, &reply, sizeof(reply), NULL, 0)) < 0) {
            return r;
        }
        return MX_OK;
    }
    case DC_OP_STATUS: {
        if (hcount != 0) {
            goto fail_wrong_hcount;
        }
        // all of these return directly and do not write a
        // reply, since this message is a reply itself
        pending_t* pending = list_remove_tail_type(&dev->pending, pending_t, node);
        if (pending == NULL) {
            log(ERROR, "devcoord: rpc: spurious status message\n");
            return MX_OK;
        }
        switch (pending->op) {
        case PENDING_BIND:
            if (msg.status != MX_OK) {
                log(ERROR, "devcoord: rpc: bind-driver '%s' status %d\n",
                    dev->name, msg.status);
            } else {
                dc_notify(dev, DEVMGR_OP_DEVICE_CHANGED);
            }
            //TODO: try next driver, clear BOUND flag
            break;
        }
        free(pending);
        return MX_OK;
    }

    default:
        log(ERROR, "devcoord: invalid rpc op %08x\n", msg.op);
        r = MX_ERR_NOT_SUPPORTED;
        goto fail_close_handles;
    }

done:
    dcs.status = r;
    if ((r = mx_channel_write(dev->hrpc, 0, &dcs, sizeof(dcs), NULL, 0)) < 0) {
        return r;
    }
    return MX_OK;

disconnect:
    dcs.status = MX_OK;
    mx_channel_write(dev->hrpc, 0, &dcs, sizeof(dcs), NULL, 0);
    return MX_ERR_STOP;

fail_wrong_hcount:
    r = MX_ERR_INVALID_ARGS;
fail_close_handles:
    while (hcount > 0) {
        mx_handle_close(hin[--hcount]);
    }
    goto done;
}

#define dev_from_ph(ph) containerof(ph, device_t, ph)

// handle inbound RPCs from devhost to devices
static mx_status_t dc_handle_device(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    device_t* dev = dev_from_ph(ph);

    if (signals & MX_CHANNEL_READABLE) {
        mx_status_t r;
        if ((r = dc_handle_device_read(dev)) < 0) {
            if (r != MX_ERR_STOP) {
                log(ERROR, "devcoord: device %p name='%s' rpc status: %d\n",
                    dev, dev->name, r);
            }
            dc_remove_device(dev, true);
            return MX_ERR_STOP;
        }
        return MX_OK;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        log(ERROR, "devcoord: device %p name='%s' disconnected!\n", dev, dev->name);
        dc_remove_device(dev, true);
        return MX_ERR_STOP;
    }
    log(ERROR, "devcoord: no work? %08x\n", signals);
    return MX_OK;
}

// send message to devhost, requesting the creation of a device
static mx_status_t dh_create_device(device_t* dev, devhost_t* dh,
                                    const char* args) {
    dc_msg_t msg;
    uint32_t mlen;
    mx_status_t r;

    // Where to get information to send to devhost from?
    // Shadow devices defer to the device they're shadowing,
    // otherwise we use the information from the device itself.
    device_t* info = (dev->flags & DEV_CTX_SHADOW) ? dev->parent : dev;
    const char* libname = info->libname;

    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, libname, args)) < 0) {
        return r;
    }

    uint32_t hcount = 0;
    mx_handle_t handle[3], hrpc;
    if ((r = mx_channel_create(0, handle, &hrpc)) < 0) {
        return r;
    }
    hcount++;

    if (libname[0]) {
        if ((r = libname_to_vmo(libname, handle + 1)) < 0) {
            goto fail;
        }
        hcount++;
        msg.op = DC_OP_CREATE_DEVICE;
    } else {
        msg.op = DC_OP_CREATE_DEVICE_STUB;
    }

    if (info->hrsrc != MX_HANDLE_INVALID) {
        if ((r = mx_handle_duplicate(info->hrsrc, MX_RIGHT_SAME_RIGHTS, handle + hcount)) < 0) {
            goto fail;
        }
        hcount++;
    }

    msg.txid = 0;
    msg.protocol_id = dev->protocol_id;

    if ((r = mx_channel_write(dh->hrpc, 0, &msg, mlen, handle, hcount)) < 0) {
        goto fail;
    }

    dev->hrpc = hrpc;
    dev->ph.handle = hrpc;
    dev->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    dev->ph.func = dc_handle_device;
    if ((r = port_wait(&dc_port, &dev->ph)) < 0) {
        goto fail_watch;
    }
    dev->host = dh;
    dh->refcount++;
    list_add_tail(&dh->devices, &dev->dhnode);
    return MX_OK;

fail:
    while (hcount > 0) {
        mx_handle_close(handle[--hcount]);
    }
fail_watch:
    mx_handle_close(hrpc);
    return r;
}

static mx_status_t dc_create_shadow(device_t* parent) {
    if (parent->shadow != NULL) {
        return MX_OK;
    }

    size_t namelen = strlen(parent->name);
    size_t liblen = strlen(parent->libname);
    device_t* dev = calloc(1, sizeof(device_t) + namelen + liblen + 2);
    if (dev == NULL) {
        return MX_ERR_NO_MEMORY;
    }
    char* text = (char*) (dev + 1);
    memcpy(text, parent->name, namelen + 1);
    dev->name = text;
    text += namelen + 1;
    memcpy(text, parent->libname, liblen + 1);
    dev->libname = text;

    list_initialize(&dev->children);
    list_initialize(&dev->pending);
    dev->flags = DEV_CTX_SHADOW;
    dev->protocol_id = parent->protocol_id;
    dev->parent = parent;
    dev->refcount = 1;
    parent->shadow = dev;
    parent->refcount++;
    log(DEVLC, "devcoord: dev %p name='%s' ++ref=%d (shadow)\n",
        parent, parent->name, parent->refcount);
    return MX_OK;
}

// send message to devhost, requesting the binding of a driver to a device
static mx_status_t dh_bind_driver(device_t* dev, const char* libname) {
    dc_msg_t msg;
    uint32_t mlen;

    pending_t* pending = malloc(sizeof(pending_t));
    if (pending == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    mx_status_t r;
    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, libname, NULL)) < 0) {
        free(pending);
        return r;
    }

    mx_handle_t vmo;
    if ((r = libname_to_vmo(libname, &vmo)) < 0) {
        free(pending);
        return r;
    }

    msg.txid = 0;
    msg.op = DC_OP_BIND_DRIVER;

    if ((r = mx_channel_write(dev->hrpc, 0, &msg, mlen, &vmo, 1)) < 0) {
        free(pending);
        return r;
    }

    dev->flags |= DEV_CTX_BOUND;
    pending->op = PENDING_BIND;
    pending->ctx = NULL;
    list_add_tail(&dev->pending, &pending->node);
    return MX_OK;
}

static mx_status_t dc_attempt_bind(driver_t* drv, device_t* dev) {
    // cannot bind driver to already bound device
    if ((dev->flags & DEV_CTX_BOUND) && (!(dev->flags & DEV_CTX_MULTI_BIND))) {
        return MX_ERR_BAD_STATE;
    }
    if (!(dev->flags & DEV_CTX_BUSDEV)) {
        // non-busdev is pretty simple
        if (dev->host == NULL) {
            log(ERROR, "devcoord: can't bind to device without devhost\n");
            return MX_ERR_BAD_STATE;
        }
        return dh_bind_driver(dev, drv->libname);
    }

    // busdev args are "processname,args"
    const char* arg0 = (dev->flags & DEV_CTX_SHADOW) ? dev->parent->args : dev->args;
    const char* arg1 = strchr(arg0, ',');
    if (arg1 == NULL) {
        return MX_ERR_INTERNAL;
    }
    size_t arg0len = arg1 - arg0;
    arg1++;

    char devhostname[32];
    snprintf(devhostname, sizeof(devhostname), "devhost:%.*s", (int) arg0len, arg0);

    mx_status_t r;
    if ((r = dc_create_shadow(dev)) < 0) {
        log(ERROR, "devcoord: cannot create shadow device: %d\n", r);
        return r;
    }

    // if this device has no devhost, first instantiate it
    if (dev->shadow->host == NULL) {
        if ((r = dc_new_devhost(devhostname, &dev->shadow->host)) < 0) {
            log(ERROR, "devcoord: dh_new_devhost: %d\n", r);
            return r;
        }
        if ((r = dh_create_device(dev->shadow, dev->shadow->host, arg1)) < 0) {
            log(ERROR, "devcoord: dh_create_device: %d\n", r);
            return r;
        }
    }

    return dh_bind_driver(dev->shadow, drv->libname);
}

static void dc_handle_new_device(device_t* dev) {
    driver_t* drv;

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

// device binding program that pure (parentless)
// misc devices use to get published in the misc devhost
static struct mx_bind_inst misc_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT);

static bool is_misc_driver(driver_t* drv) {
    return (drv->binding_size == sizeof(misc_device_binding)) &&
        (memcmp(&misc_device_binding, drv->binding, sizeof(misc_device_binding)) == 0);
}

// device binding program that special root-level
// devices use to get published in the root devhost
static struct mx_bind_inst root_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ROOT);

static bool is_root_driver(driver_t* drv) {
    return (drv->binding_size == sizeof(root_device_binding)) &&
        (memcmp(&root_device_binding, drv->binding, sizeof(root_device_binding)) == 0);
}

static bool is_acpi_bus_driver(driver_t* drv) {
    // only our built-in acpi driver should bind as acpi bus
    // so compare library path instead of binding program
    return !strcmp(drv->libname, "/boot/driver/bus-acpi.so");
}

static bool is_platform_bus_driver(driver_t* drv) {
    // only our built-in platform-bus driver should bind as platform bus
    // so compare library path instead of binding program
    return !strcmp(drv->libname, "/boot/driver/platform-bus.so");
}

static work_t new_driver_work;

void dc_driver_added(driver_t* drv, const char* version) {
    if (dc_running) {
        list_add_head(&list_drivers_new, &drv->node);
        if (new_driver_work.op == WORK_IDLE) {
            queue_work(&new_driver_work, WORK_DRIVER_ADDED, 0);
        }
        return;
    }
    if (version[0] == '!') {
        // debugging / development hack
        // prioritize drivers with version "!..." over others
        list_add_head(&list_drivers, &drv->node);
    } else {
        list_add_tail(&list_drivers, &drv->node);
    }
}

device_t* coordinator_init(mx_handle_t root_job) {
    printf("coordinator_init()\n");

    mx_status_t status = mx_job_create(root_job, 0u, &devhost_job);
    if (status < 0) {
        log(ERROR, "devcoord: unable to create devhost job\n");
    }
    static const mx_policy_basic_t policy[] = {
        { MX_POL_BAD_HANDLE, MX_POL_ACTION_EXCEPTION },
    };
    status = mx_job_set_policy(devhost_job, MX_JOB_POL_RELATIVE,
                               MX_JOB_POL_BASIC, &policy, countof(policy));
    if (status < 0) {
        log(ERROR, "devcoord: mx_job_set_policy() failed\n");
    }
    mx_object_set_property(devhost_job, MX_PROP_NAME, "magenta-drivers", 15);

    port_init(&dc_port);

    return &root_device;
}

//TODO: The acpisvc needs to become the acpi bus device
//      For now, we launch it manually here so PCI can work
static void acpi_init(void) {
    mx_status_t status = mx_channel_create(0, &acpi_rpc[0], &acpi_rpc[1]);
    if (status != MX_OK) {
        return;
    }
    devhost_acpi_set_rpc(acpi_rpc[0]);
}

void dc_bind_driver(driver_t* drv) {
    if (dc_running) {
        printf("devcoord: driver '%s' added\n", drv->name);
    }
    if (is_root_driver(drv)) {
        dc_attempt_bind(drv, &root_device);
    } else if (is_misc_driver(drv)) {
        dc_attempt_bind(drv, &misc_device);
    } else if (is_acpi_bus_driver(drv)) {
        dc_attempt_bind(drv, &acpi_device);
    } else if (is_platform_bus_driver(drv) &&
               (platform_device.hrsrc != MX_HANDLE_INVALID)) {
        dc_attempt_bind(drv, &platform_device);
    } else if (dc_running) {
        device_t* dev;
        list_for_every_entry(&list_devices, dev, device_t, anode) {
            if (dev->flags & (DEV_CTX_BOUND | DEV_CTX_DEAD | DEV_CTX_ZOMBIE)) {
                // if device is already bound or being destroyed, skip it
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

static bool system_available;
static bool system_loaded;

static mx_status_t dc_control_event(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    switch (evt) {
    case CTL_SCAN_SYSTEM:
        if (!system_loaded) {
            system_loaded = true;
            find_loadable_drivers("/system/driver");
            find_loadable_drivers("/system/lib/driver");
        }
        break;
    }
    return MX_OK;
}

static port_handler_t control_handler = {
    .func = dc_control_event,
};


void load_system_drivers(void) {
    system_available = true;
    port_queue(&dc_port, &control_handler, CTL_SCAN_SYSTEM);
}

void coordinator(void) {
    log(INFO, "devmgr: coordinator()\n");

    if (getenv_bool("devmgr.verbose", false)) {
        log_flags |= LOG_DEVLC;
    }

// TODO(MG-1074): Conditionally initialize ACPI if it is present.
#if defined(__x86_64__)
    acpi_init();
#endif

    devfs_publish(&root_device, &misc_device);
    devfs_publish(&root_device, &socket_device);
    devfs_publish(&root_device, &acpi_device);
    if (platform_device.hrsrc != MX_HANDLE_INVALID) {
        devfs_publish(&root_device, &platform_device);
    }

    find_loadable_drivers("/boot/driver");
    find_loadable_drivers("/boot/driver/test");
    find_loadable_drivers("/boot/lib/driver");

    // Special case early handling for the ramdisk boot
    // path where /system is present before the coordinator
    // starts.  This avoids breaking the "priority hack" and
    // can be removed once the real driver priority system
    // exists.
    if (system_available) {
        dc_control_event(&control_handler, 0, CTL_SCAN_SYSTEM);
    }

    driver_t* drv;
    list_for_every_entry(&list_drivers, drv, driver_t, node) {
        dc_bind_driver(drv);
    }

    dc_running = true;

    for (;;) {
        mx_status_t status;
        if (list_is_empty(&list_pending_work)) {
            status = port_dispatch(&dc_port, MX_TIME_INFINITE, true);
        } else {
            status = port_dispatch(&dc_port, 0, true);
            if (status == MX_ERR_TIMED_OUT) {
                process_work(list_remove_head_type(&list_pending_work, work_t, node));
                continue;
            }
        }
        if (status != MX_OK) {
            log(ERROR, "devcoord: port dispatch ended: %d\n", status);
        }
    }
}
