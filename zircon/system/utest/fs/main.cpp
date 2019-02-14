// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fuchsia/device/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/unsafe.h>
#include <lib/memfs/memfs.h>
#include <unittest/unittest.h>

#include "filesystems.h"

const char* filesystem_name_filter = "";

static void print_test_help(FILE* f) {
    fprintf(f,
            "  -d <blkdev>\n"
            "      Use block device <blkdev> instead of a ramdisk\n"
            "\n"
            "  -f <fs>\n"
            "      Test only fileystem <fs>, where <fs> is one of:\n");
    for (int j = 0; j < NUM_FILESYSTEMS; j++) {
        fprintf(f, "%8s%s\n", "", FILESYSTEMS[j].name);
    }
}

int main(int argc, char** argv) {
    use_real_disk = false;

    unittest_register_test_help_printer(print_test_help);

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-d") == 0 && (i + 1 < argc)) {
            fbl::unique_fd fd(open(argv[i + 1], O_RDWR));
            if (!fd) {
                fprintf(stderr, "[fs] Could not open block device\n");
                return -1;
            }
            fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
            if (io == nullptr) {
                fprintf(stderr, "[fs] could not convert fd to io\n");
                return -1;
            }

            zx_status_t call_status;
            size_t path_len;
            zx_status_t status = fuchsia_device_ControllerGetTopologicalPath(
                    fdio_unsafe_borrow_channel(io), &call_status, test_disk_path, PATH_MAX - 1,
                    &path_len);
            fdio_unsafe_release(io);
            if (status == ZX_OK) {
                status = call_status;
            }
            if (status != ZX_OK) {
                fprintf(stderr, "[fs] Could not acquire topological path of block device\n");
                return -1;
            }
            test_disk_path[path_len] = 0;

            if (ioctl_block_get_info(fd.get(), &test_disk_info) < 0) {
                fprintf(stderr, "[fs] Could not read disk info\n");
                return -1;
            }
            // If we previously tried running tests on this disk, it may
            // have created an FVM and failed. (Try to) clean up from previous state
            // before re-running.
            fvm_destroy(test_disk_path);
            use_real_disk = true;
            i += 2;
        } else if (!strcmp(argv[i], "-f") && (i + 1 < argc)) {
            bool found = false;
            for (int j = 0; j < NUM_FILESYSTEMS; j++) {
                if (!strcmp(argv[i + 1], FILESYSTEMS[j].name)) {
                    found = true;
                    filesystem_name_filter = argv[i + 1];
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "Error: Filesystem not found\n");
                return -1;
            }
            i += 2;
        } else {
            // Ignore options we don't recognize. See ulib/unittest/README.md.
            break;
        }
    }

    // Initialize tmpfs.
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    if (loop.StartThread() != ZX_OK) {
        fprintf(stderr, "Error: Cannot initialize local tmpfs loop\n");
        return -1;
    }
    if (memfs_install_at(loop.dispatcher(), kTmpfsPath) != ZX_OK) {
        fprintf(stderr, "Error: Cannot install local tmpfs\n");
        return -1;
    }

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
