// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs/vfs.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <blobstore/fsck.h>

#ifdef __Fuchsia__
#include <async/loop.h>
#include <blobstore/blobstore.h>
#else
#include <blobstore/host.h>
#endif

namespace {

#ifdef __Fuchsia__
#define MIN_ARGS 2
#else
#define MIN_ARGS 3
#endif

typedef struct {
    bool readonly = false;
    uint64_t data_blocks = 0;
    fbl::Vector<fbl::String> blob_list;
} blob_options_t;

#ifdef __Fuchsia__

int do_blobstore_mount(fbl::unique_fd fd, const blob_options_t& options) {
    bool readonly = options.readonly;
    if (!readonly) {
        block_info_t block_info;
        zx_status_t status = static_cast<zx_status_t>(ioctl_block_get_info(fd.get(), &block_info));
        if (status < ZX_OK) {
            FS_TRACE_ERROR("blobstore: Unable to query block device, fd: %d status: 0x%x\n",
                            fd.get(), status);
            return -1;
        }
        readonly = block_info.flags & BLOCK_FLAG_READONLY;
    }

    fbl::RefPtr<blobstore::VnodeBlob> vn;
    if (blobstore::blobstore_mount(&vn, fbl::move(fd)) < 0) {
        return -1;
    }
    zx_handle_t h = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (h == ZX_HANDLE_INVALID) {
        FS_TRACE_ERROR("blobstore: Could not access startup handle to mount point\n");
        return h;
    }

    async::Loop loop;
    fs::Vfs vfs(loop.async());
    vfs.SetReadonly(readonly);
    zx_status_t status;
    if ((status = vfs.ServeDirectory(fbl::move(vn), zx::channel(h))) != ZX_OK) {
        return status;
    }
    loop.Run();
    return 0;
}

#else

int do_blobstore_add_blob(blobstore::Blobstore* bs, const char* blob_name) {
    fbl::unique_fd data_fd(open(blob_name, O_RDONLY, 0644));
    if (!data_fd) {
        fprintf(stderr, "error: cannot open '%s'\n", blob_name);
        return -1;
    }
    int r;
    if ((r = blobstore::blobstore_add_blob(bs, data_fd.get())) != 0) {
        if (r != ZX_ERR_ALREADY_EXISTS) {
            fprintf(stderr, "blobstore: Failed to add blob '%s': %d\n", blob_name, r);
            return -1;
        }
    }
    return 0;
}

int do_blobstore_add_blobs(fbl::unique_fd fd, const blob_options_t& options) {
    if (options.blob_list.is_empty()) {
        fprintf(stderr, "Adding a blob requires an additional file argument\n");
        return -1;
    }

    fbl::RefPtr<blobstore::Blobstore> bs;
    if (blobstore_create(&bs, fbl::move(fd)) < 0) {
        return -1;
    }

    for (unsigned i = 0; i < options.blob_list.size(); i++) {
        if (do_blobstore_add_blob(bs.get(), options.blob_list[i].c_str()) < 0) {
            return -1;
        }
    }

    return 0;
}

#endif

int do_blobstore_mkfs(fbl::unique_fd fd, const blob_options_t& options) {
    uint64_t block_count;
    if (blobstore::blobstore_get_blockcount(fd.get(), &block_count)) {
        fprintf(stderr, "blobstore: cannot find end of underlying device\n");
        return -1;
    }

    int r = blobstore::blobstore_mkfs(fd.get(), block_count);

#ifndef __Fuchsia__
    if (r >= 0 && !options.blob_list.is_empty()) {
        if (do_blobstore_add_blobs(fbl::move(fd), options) < 0) {
            return -1;
        }
    }
#endif
    return r;
}

int do_blobstore_check(fbl::unique_fd fd, const blob_options_t& options) {
    fbl::RefPtr<blobstore::Blobstore> vn;
    if (blobstore::blobstore_create(&vn, fbl::move(fd)) < 0) {
        return -1;
    }

    return blobstore::blobstore_check(vn);
}

#ifndef __Fuchsia__
off_t calculate_total_size(off_t data_blocks) {
    blobstore::blobstore_info_t info;
    info.block_count = data_blocks;
    info.inode_count = 32768;
    return (data_blocks + blobstore::DataStartBlock(info)) * blobstore::kBlobstoreBlockSize;
}

off_t calculate_blob_blocks(off_t data_size) {
    blobstore::blobstore_inode_t node;
    node.blob_size = data_size;
    return MerkleTreeBlocks(node) + BlobDataBlocks(node);
}

zx_status_t process_blob(char* blob_name, blob_options_t* options) {
    struct stat s;
    if (stat(blob_name, &s) < 0) {
        fprintf(stderr, "Failed to stat blob %s\n", blob_name);
        return ZX_ERR_IO;
    }

    options->data_blocks += calculate_blob_blocks(s.st_size);
    options->blob_list.push_back(blob_name);
    return ZX_OK;
}

zx_status_t process_manifest_line(FILE* manifest, const char* dir_path, blob_options_t* options) {
    size_t size = 0;
    char* line = nullptr;

    int r = getline(&line, &size, manifest);

    if (r < 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    fbl::unique_free_ptr<char> ptr(line);

    // Exit early if line is commented out
    if (line[0] == '#') {
        return ZX_OK;
    }

    char src[PATH_MAX];
    strncpy(src, dir_path, PATH_MAX);
    strncat(src, "/", PATH_MAX - strlen(src));

    char* eq_ptr = strchr(line, '=');
    if (eq_ptr == nullptr) {
        strncat(src, line, PATH_MAX - strlen(src));
    } else {
        if (strchr(eq_ptr + 1, '=') != nullptr) {
            fprintf(stderr, "Too many '=' in input\n");
            return ZX_ERR_INVALID_ARGS;
        }
        strncat(src, eq_ptr + 1, PATH_MAX - strlen(src));
    }

    char* nl_ptr = strchr(src, '\n');
    if (nl_ptr != nullptr) {
        *nl_ptr = '\0';
    }

    return process_blob(src, options);
}

int process_manifest(char* manifest_path, blob_options_t* options) {
    fbl::unique_fd manifest_fd(open(manifest_path, O_RDONLY, 0644));
    if (!manifest_fd) {
        fprintf(stderr, "error: cannot open '%s'\n", manifest_path);
        return -1;
    }

    char dir_path[PATH_MAX];
    strncpy(dir_path, dirname(manifest_path), PATH_MAX);
    FILE* manifest = fdopen(manifest_fd.release(), "r");
    while (true) {
        zx_status_t status = process_manifest_line(manifest, dir_path, options);
        if (status == ZX_ERR_OUT_OF_RANGE) {
            fclose(manifest);
            return 0;
        } else if (status != ZX_OK) {
            fclose(manifest);
            return -1;
        }
    }
}
#endif

typedef int (*CommandFunction)(fbl::unique_fd fd, const blob_options_t& options);

struct {
    const char* name;
    CommandFunction func;
    bool edit_file;
    bool accept_args;
    const char* help;
} CMDS[] = {
    {"create", do_blobstore_mkfs, true, true, "initialize filesystem"},
    {"mkfs", do_blobstore_mkfs, true, true, "initialize filesystem"},
    {"check", do_blobstore_check, false, false, "check filesystem integrity"},
    {"fsck", do_blobstore_check, false, false, "check filesystem integrity"},
#ifdef __Fuchsia__
    {"mount", do_blobstore_mount, false, false, "mount filesystem"},
#else
    {"add", do_blobstore_add_blobs, false, true, "add blobs to a blobstore image"},
#endif
};

int usage() {
    fprintf(stderr,
#ifdef __Fuchsia__
            "usage: blobstore [ <options>* ] <command> [ <arg>* ]\n"
            "\n"
            "options: --readonly  Mount filesystem read-only\n"
            "\n"
            "On Fuchsia, blobstore takes the block device argument by handle.\n"
            "This can make 'blobstore' commands hard to invoke from command line.\n"
            "Try using the [mkfs,fsck,mount,umount] commands instead\n"
#else
            "usage: blobstore <file-or-device>[@<size>] <command> [ <arg>* ]\n"
#endif
            "\n");
    for (unsigned n = 0; n < (sizeof(CMDS) / sizeof(CMDS[0])); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:",
                CMDS[n].name, CMDS[n].help);
    }
    fprintf(stderr, "\n");
#ifndef __Fuchsia__
    fprintf(stderr, "arguments (valid for create, one or more required for add):\n"
                    "\t--blob <path-to-file>\n"
                    "\t--manifest <path-to-manifest>\n");
#endif
    return -1;
}

// Process options/commands and return open fd to device
int process_args(int argc, char** argv, CommandFunction* func, blob_options_t* options) {
    if (argc < MIN_ARGS) {
        fprintf(stderr, "Not enough args\n");
        return usage();
    }

    argc--;
    argv++;

    // Read options
    while (argc > 1) {
        if (!strcmp(argv[0], "--readonly")) {
            options->readonly = true;
        } else {
            break;
        }
        argc--;
        argv++;
    }

#ifndef __Fuchsia__
    char* device = argv[0];
    argc--;
    argv++;
#endif

    char* command = argv[0];

    __UNUSED bool edit_file = false;
    __UNUSED bool accept_args = false;
    // Validate command
    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(command, CMDS[i].name)) {
            *func = CMDS[i].func;
            edit_file = CMDS[i].edit_file;
            accept_args = CMDS[i].accept_args;
        } else if (!strcmp(command, "manifest") && !strcmp(CMDS[i].name, "add")) {
            // Temporary hack to allow manifest command to use add function
            *func = CMDS[i].func;
            edit_file = CMDS[i].edit_file;
            accept_args = CMDS[i].accept_args;
        }
    }

    if (*func == nullptr) {
        fprintf(stderr, "Unknown command: %s\n", argv[0]);
        return usage();
    }

    argc--;
    argv++;

#ifdef __Fuchsia__
    // Block device passed by handle
    return FS_FD_BLOCKDEVICE;
#else
    // Process arguments
    while (argc > 0) {
        if (!accept_args) {
            fprintf(stderr, "Arguments not allowed\n");
            return usage();
        } else if (argc == 1 && !edit_file && options->blob_list.is_empty()) {
            // Temporary hack to allow previous syntax for add/manifest
            if (!strcmp(command, "add")) {
                if (process_blob(argv[0], options) < 0) {
                    return -1;
                }
            } else if (!strcmp(command, "manifest")) {
                if (process_manifest(argv[0], options) < 0) {
                    return -1;
                }
            } else {
                return usage();
            }
        } else if (argc < 2) {
            fprintf(stderr, "Invalid arguments\n");
            return usage();
        } else if (!strcmp(argv[0], "--blob")) {
            if (process_blob(argv[1], options) < 0) {
                return -1;
            }
        } else if (!strcmp(argv[0], "--manifest")) {
            if (process_manifest(argv[1], options) < 0) {
                return -1;
            }
        } else {
            fprintf(stderr, "Argument not found: %s\n", argv[0]);
            return usage();
        }

        argc -= 2;
        argv += 2;
    }

    // Determine blobstore size (with no de-duping for identical files)
    off_t total_size = calculate_total_size(options->data_blocks);
    char* sizestr = nullptr;
    if ((sizestr = strchr(device, '@')) != nullptr) {
        if (!edit_file) {
            fprintf(stderr, "Cannot specify size for this command\n");
            return -1;
        }
        // Create a file with an explicitly requested size
        *sizestr++ = 0;
        char* end;
        size_t size = strtoull(sizestr, &end, 10);
        if (end == sizestr) {
            fprintf(stderr, "blobstore: bad size: %s\n", sizestr);
            return usage();
        }
        switch (end[0]) {
        case 'M':
        case 'm':
            size *= (1024 * 1024);
            end++;
            break;
        case 'G':
        case 'g':
            size *= (1024 * 1024 * 1024);
            end++;
            break;
        }
        if (end[0]) {
            fprintf(stderr, "blobstore: bad size: %s\n", sizestr);
            return usage();
        }

        if (size < total_size) {
            fprintf(stderr, "Specified size is not large enough\n");
            return -1;
        }

        total_size = size;
    }

    int open_flags = O_RDWR;

    if (edit_file) {
        open_flags |= O_CREAT;
    }

    fbl::unique_fd fd(open(device, open_flags, 0644));
    if (!fd) {
        fprintf(stderr, "error: cannot open '%s'\n", device);
        return -1;
    }

    struct stat s;
    if (fstat(fd.get(), &s) < 0) {
        fprintf(stderr, "Failed to stat blob %s\n", device);
        return -1;
    }

    // Update size
    if (edit_file && (sizestr != nullptr || s.st_size < total_size)) {
        if (ftruncate(fd.get(), total_size)) {
            fprintf(stderr, "error: cannot truncate device '%s'\n", device);
            return -1;
        }
    }

    return fd.release();
#endif
}
} // namespace

int main(int argc, char** argv) {
    CommandFunction func = nullptr;
    blob_options_t options;
    fbl::unique_fd fd(process_args(argc, argv, &func, &options));

    if (!fd) {
        return -1;
    }

    return func(fbl::move(fd), options);
}
