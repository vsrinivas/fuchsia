// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/time.h>
#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/device/vfs.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fs-management/ramdisk.h>

#define RAMCTL_PATH "/dev/misc/ramctl"
#define BLOCK_EXTENSION "block"

static zx_status_t driver_watcher_cb(int dirfd, int event, const char* fn, void* cookie) {
    char* wanted = static_cast<char*>(cookie);
    if (event == WATCH_EVENT_ADD_FILE && strcmp(fn, wanted) == 0) {
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

static zx_status_t wait_for_device_impl(char* path, const zx::time& deadline) {
    zx_status_t rc;

    // Peel off last path segment
    char* sep = strrchr(path, '/');
    if (path[0] == '\0' || (!sep)) {
        fprintf(stderr, "invalid device path '%s'\n", path);
        return ZX_ERR_BAD_PATH;
    }
    char* last = sep + 1;

    *sep = '\0';
    auto restore_path = fbl::MakeAutoCall([sep] { *sep = '/'; });

    // Recursively check the path up to this point
    struct stat buf;
    if (stat(path, &buf) != 0 && (rc = wait_for_device_impl(path, deadline)) != ZX_OK) {
        fprintf(stderr, "failed to bind '%s': %s\n", path, zx_status_get_string(rc));
        return rc;
    }

    // Early exit if this segment is empty
    if (last[0] == '\0') {
        return ZX_OK;
    }

    // Open the parent directory
    DIR* dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "unable to open '%s'\n", path);
        return ZX_ERR_NOT_FOUND;
    }
    auto close_dir = fbl::MakeAutoCall([&] { closedir(dir); });

    // Wait for the next path segment to show up
    rc = fdio_watch_directory(dirfd(dir), driver_watcher_cb, deadline.get(), last);
    if (rc != ZX_ERR_STOP) {
        fprintf(stderr, "error when waiting for '%s': %s\n", last, zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

struct ramdisk_client {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(ramdisk_client);

    static zx_status_t Create(const char* instance_name,
                              zx::duration duration,
                              std::unique_ptr<ramdisk_client>* out) {
        fbl::String ramdisk_path = fbl::StringPrintf("%s/%s", RAMCTL_PATH, instance_name);
        fbl::unique_fd ramdisk_fd(open(ramdisk_path.c_str(), O_RDWR));
        if (!ramdisk_fd) {
            return ZX_ERR_BAD_STATE;
        }

        // If binding to the block interface fails, ensure we still try to tear down the
        // ramdisk driver.
        auto cleanup = fbl::MakeAutoCall([&ramdisk_fd]() {
            ramdisk_client::DestroyByFd(ramdisk_fd);
        });

        fbl::String path = fbl::String::Concat({ramdisk_path, "/", BLOCK_EXTENSION});
        zx_status_t status = wait_for_device(path.c_str(), duration.get());
        if (status != ZX_OK) {
            return status;
        }
        fbl::unique_fd block_fd(open(path.c_str(), O_RDWR));
        if (!block_fd) {
            return ZX_ERR_BAD_STATE;
        }

        cleanup.cancel();
        *out = std::unique_ptr<ramdisk_client>(new ramdisk_client(std::move(path),
                                                                  std::move(ramdisk_fd),
                                                                  std::move(block_fd)));
        return ZX_OK;
    }

    zx_status_t Rebind() {
        ssize_t r = ioctl_block_rr_part(block_fd_.get());
        if (r < 0) {
            return static_cast<zx_status_t>(r);
        }
        block_fd_.reset();
        ramdisk_fd_.reset();

        // Ramdisk paths have the form: /dev/.../ramctl/ramdisk-xxx/block.
        // To rebind successfully, first, we rebind the "ramdisk-xxx" path,
        // and then we wait for "block" to rebind.

        // Wait for the "ramdisk-xxx" path to rebind.
        const char* sep = strrchr(path_.c_str(), '/');
        char ramdisk_path[PATH_MAX];
        strlcpy(ramdisk_path, path_.c_str(), sep - path_.c_str() + 1);
        zx_status_t status = wait_for_device_impl(ramdisk_path,
                                                  zx::deadline_after(zx::sec(3)));
        if (status != ZX_OK) {
            return status;
        }

        ramdisk_fd_.reset(open(ramdisk_path, O_RDWR));
        if (!ramdisk_fd_) {
            return ZX_ERR_BAD_STATE;
        }

        // Wait for the "block" path to rebind.
        strlcpy(ramdisk_path, path_.c_str(), sizeof(ramdisk_path));
        status = wait_for_device_impl(ramdisk_path, zx::deadline_after(zx::sec(3)));
        if (status != ZX_OK) {
            return status;
        }
        block_fd_.reset(open(path_.c_str(), O_RDWR));
        if (!block_fd_) {
            return ZX_ERR_BAD_STATE;
        }
        return ZX_OK;
    }

    zx_status_t Destroy() {
        if (!ramdisk_fd_) {
            return ZX_ERR_BAD_STATE;
        }

        zx_status_t status = DestroyByFd(ramdisk_fd_);
        if (status != ZX_OK) {
            return status;
        }
        ramdisk_fd_.reset();
        block_fd_.reset();
        return ZX_OK;
    }

    const fbl::unique_fd& RamdiskFd() const {
        return ramdisk_fd_;
    }

    const fbl::unique_fd& BlockFd() const {
        return block_fd_;
    }

    const fbl::String& Path() const {
        return path_;
    }

    ~ramdisk_client() {
        Destroy();
    }

private:
    ramdisk_client(fbl::String path, fbl::unique_fd ramdisk_fd, fbl::unique_fd block_fd)
        : path_(std::move(path)),
          ramdisk_fd_(std::move(ramdisk_fd)),
          block_fd_(std::move(block_fd)) {}

    static zx_status_t DestroyByFd(const fbl::unique_fd& fd) {
        ssize_t r = ioctl_ramdisk_unlink(fd.get());
        if (r != ZX_OK) {
            return static_cast<zx_status_t>(r);
        }
        return ZX_OK;
    }

    fbl::String path_;
    fbl::unique_fd ramdisk_fd_;
    fbl::unique_fd block_fd_;
};

// TODO(aarongreen): This is more generic than just fs-management, or even block devices.  Move this
// (and its tests) out of ramdisk and to somewhere else?
zx_status_t wait_for_device(const char* path, zx_duration_t timeout) {
    if (!path || timeout == 0) {
        fprintf(stderr, "invalid args: path='%s', timeout=%" PRIu64 "\n", path, timeout);
        return ZX_ERR_INVALID_ARGS;
    }

    // Make a mutable copy
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    zx::time deadline = zx::deadline_after(zx::duration(timeout));
    return wait_for_device_impl(tmp, deadline);
}

static int open_ramctl(void) {
    int fd = open(RAMCTL_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramctl\n");
    }
    return fd;
}

static zx_status_t finish_create(ramdisk_ioctl_config_response_t* response, ssize_t r,
                                 struct ramdisk_client** out) {
    if (r < 0) {
        fprintf(stderr, "Could not configure ramdev\n");
        return ZX_ERR_INVALID_ARGS;
    }
    response->name[r] = 0;

    std::unique_ptr<ramdisk_client> client;
    zx_status_t status = ramdisk_client::Create(response->name, zx::sec(3), &client);
    if (status != ZX_OK) {
        return status;
    }

    *out = client.release();
    return ZX_OK;
}

zx_status_t create_ramdisk(uint64_t blk_size, uint64_t blk_count, ramdisk_client** out) {
    fbl::unique_fd fd(open_ramctl());
    if (fd.get() < 0) {
        return ZX_ERR_BAD_STATE;
    }
    ramdisk_ioctl_config_t config = {};
    config.blk_size = blk_size;
    config.blk_count = blk_count;
    memset(config.type_guid, 0, ZBI_PARTITION_GUID_LEN);
    ramdisk_ioctl_config_response_t response;
    return finish_create(&response, ioctl_ramdisk_config(fd.get(), &config, &response), out);
}

zx_status_t create_ramdisk_with_guid(uint64_t blk_size, uint64_t blk_count,
                                     const uint8_t* type_guid, size_t guid_len,
                                     ramdisk_client** out) {
    fbl::unique_fd fd(open_ramctl());
    if (fd.get() < 0) {
        return ZX_ERR_BAD_STATE;
    }
    if (type_guid == NULL || guid_len < ZBI_PARTITION_GUID_LEN) {
        return ZX_ERR_INVALID_ARGS;
    }
    ramdisk_ioctl_config_t config = {};
    config.blk_size = blk_size;
    config.blk_count = blk_count;
    memcpy(config.type_guid, type_guid, ZBI_PARTITION_GUID_LEN);
    ramdisk_ioctl_config_response_t response;
    return finish_create(&response, ioctl_ramdisk_config(fd.get(), &config, &response), out);
}

zx_status_t create_ramdisk_from_vmo(zx_handle_t vmo, ramdisk_client** out) {
    fbl::unique_fd fd(open_ramctl());
    if (fd.get() < 0) {
        return ZX_ERR_BAD_STATE;
    }
    ramdisk_ioctl_config_response_t response;
    return finish_create(&response, ioctl_ramdisk_config_vmo(fd.get(), &vmo, &response), out);
}

int ramdisk_get_block_fd(const ramdisk_client_t* client) {
    return client->BlockFd().get();
}

const char* ramdisk_get_path(const ramdisk_client_t* client) {
    return client->Path().c_str();
}

zx_status_t ramdisk_sleep_after(const ramdisk_client* client, uint64_t block_count) {
    ssize_t r = ioctl_ramdisk_sleep_after(client->RamdiskFd().get(), &block_count);
    if (r != ZX_OK) {
        return static_cast<zx_status_t>(r);
    }
    return ZX_OK;
}

zx_status_t ramdisk_wake(const ramdisk_client* client) {
    ssize_t r = ioctl_ramdisk_wake_up(client->RamdiskFd().get());
    if (r != ZX_OK) {
        return static_cast<zx_status_t>(r);
    }
    return ZX_OK;
}

zx_status_t ramdisk_set_flags(const ramdisk_client* client, uint32_t flags) {
    ssize_t rc = ioctl_ramdisk_set_flags(client->RamdiskFd().get(), &flags);
    if (rc < 0) {
        return static_cast<zx_status_t>(rc);
    }
    return ZX_OK;
}

zx_status_t ramdisk_get_block_counts(const ramdisk_client* client, ramdisk_blk_counts_t* counts) {
    ssize_t rc = ioctl_ramdisk_get_blk_counts(client->RamdiskFd().get(), counts);
    if (rc < 0) {
        return static_cast<zx_status_t>(rc);
    }
    return ZX_OK;
}

zx_status_t ramdisk_rebind(ramdisk_client_t* client) {
    return client->Rebind();
}

zx_status_t ramdisk_destroy(ramdisk_client* client) {
    zx_status_t status = client->Destroy();
    delete client;
    return status;
}
