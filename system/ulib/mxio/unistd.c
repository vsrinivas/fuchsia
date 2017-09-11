// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <utime.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/device/vfs.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/namespace.h>
#include <mxio/private.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>
#include <mxio/vfs.h>
#include <mxio/socket.h>

#include "private.h"
#include "unistd.h"

static_assert(MXIO_FLAG_CLOEXEC == FD_CLOEXEC, "Unexpected mxio flags value");

// non-thread-safe emulation of unistd io functions
// using the mxio transports

mxio_state_t __mxio_global_state = {
    .lock = MTX_INIT,
    .cwd_lock = MTX_INIT,
    .init = true,
    .cwd_path = "/",
};

void mxio_install_root(mxio_t* root) {
    mtx_lock(&mxio_lock);
    if (mxio_root_init) {
        mxio_root_handle = root;
        mxio_root_init = false;
    }
    mtx_unlock(&mxio_lock);
}

// Attaches an mxio to an fdtab slot.
// The mxio must have been upref'd on behalf of the
// fdtab prior to binding.
int mxio_bind_to_fd(mxio_t* io, int fd, int starting_fd) {
    mxio_t* io_to_close = NULL;

    mtx_lock(&mxio_lock);
    if (fd < 0) {
        // A negative fd implies that any free fd value can be used
        //TODO: bitmap, ffs, etc
        for (fd = starting_fd; fd < MAX_MXIO_FD; fd++) {
            if (mxio_fdtab[fd] == NULL) {
                goto free_fd_found;
            }
        }
        errno = EMFILE;
        mtx_unlock(&mxio_lock);
        return -1;
    } else if (fd >= MAX_MXIO_FD) {
        errno = EINVAL;
        mtx_unlock(&mxio_lock);
        return -1;
    } else {
        io_to_close = mxio_fdtab[fd];
        if (io_to_close) {
            io_to_close->dupcount--;
            if (io_to_close->dupcount > 0) {
                // still alive in another fdtab slot
                mxio_release(io_to_close);
                io_to_close = NULL;
            }
        }
    }

free_fd_found:
    io->dupcount++;
    mxio_fdtab[fd] = io;
    mtx_unlock(&mxio_lock);

    if (io_to_close) {
        io_to_close->ops->close(io_to_close);
        mxio_release(io_to_close);
    }
    return fd;
}

// If a mxio_t exists for this fd and it has not been dup'd
// and is not in active use (an io operation underway, etc),
// detach it from the fdtab and return it with a single
// refcount.
mx_status_t mxio_unbind_from_fd(int fd, mxio_t** out) {
    mx_status_t status;
    mtx_lock(&mxio_lock);
    if (fd >= MAX_MXIO_FD) {
        status = MX_ERR_INVALID_ARGS;
        goto done;
    }
    mxio_t* io = mxio_fdtab[fd];
    if (io == NULL) {
        status = MX_ERR_INVALID_ARGS;
        goto done;
    }
    if (io->dupcount > 1) {
        status = MX_ERR_UNAVAILABLE;
        goto done;
    }
    if (atomic_load(&io->refcount) > 1) {
        status = MX_ERR_UNAVAILABLE;
        goto done;
    }
    io->dupcount = 0;
    mxio_fdtab[fd] = NULL;
    *out = io;
    status = MX_OK;
done:
    mtx_unlock(&mxio_lock);
    return status;
}

mxio_t* __mxio_fd_to_io(int fd) {
    if ((fd < 0) || (fd >= MAX_MXIO_FD)) {
        return NULL;
    }
    mxio_t* io = NULL;
    mtx_lock(&mxio_lock);
    if ((io = mxio_fdtab[fd]) != NULL) {
        mxio_acquire(io);
    }
    mtx_unlock(&mxio_lock);
    return io;
}

static void mxio_exit(void) {
    mtx_lock(&mxio_lock);
    for (int fd = 0; fd < MAX_MXIO_FD; fd++) {
        mxio_t* io = mxio_fdtab[fd];
        if (io) {
            mxio_fdtab[fd] = NULL;
            io->dupcount--;
            if (io->dupcount == 0) {
                io->ops->close(io);
                mxio_release(io);
            }
        }
    }
    mtx_unlock(&mxio_lock);
}

mx_status_t mxio_close(mxio_t* io) {
    if (io->dupcount > 0) {
        printf("mxio_close(%p): dupcount nonzero!\n", io);
    }
    return io->ops->close(io);
}

// Possibly return an owned mxio_t corresponding to either the root,
// the cwd, or, for the ...at variants, dirfd. In the absolute path
// case, *path is also adjusted.
static mxio_t* mxio_iodir(const char** path, int dirfd) {
    mxio_t* iodir = NULL;
    mtx_lock(&mxio_lock);
    if (*path[0] == '/') {
        iodir = mxio_root_handle;
        // Since we are sending a request to the root handle, the
        // rest of the path should be canonicalized as a relative
        // path (relative to this root handle).
        while (*path[0] == '/') {
            (*path)++;
            if (*path[0] == 0) {
                *path = ".";
            }
        }
    } else if (dirfd == AT_FDCWD) {
        iodir = mxio_cwd_handle;
    } else if ((dirfd >= 0) && (dirfd < MAX_MXIO_FD)) {
        iodir = mxio_fdtab[dirfd];
    }
    if (iodir != NULL) {
        mxio_acquire(iodir);
    }
    mtx_unlock(&mxio_lock);
    return iodir;
}

#define IS_SEPARATOR(c) ((c) == '/' || (c) == 0)

// Checks that if we increment this index forward, we'll
// still have enough space for a null terminator within
// PATH_MAX bytes.
#define CHECK_CAN_INCREMENT(i)           \
    if (unlikely((i) + 1 >= PATH_MAX)) { \
        return MX_ERR_BAD_PATH;          \
    }

// Cleans an input path, transforming it to out, according to the
// rules defined by "Lexical File Names in Plan 9 or Getting Dot-Dot Right",
// accessible at: https://9p.io/sys/doc/lexnames.html
//
// Code heavily inspired by Go's filepath.Clean function, from:
// https://golang.org/src/path/filepath/path.go
//
// out is expected to be PATH_MAX bytes long.
// Sets is_dir to 'true' if the path is a directory, and 'false' otherwise.
mx_status_t __mxio_cleanpath(const char* in, char* out, size_t* outlen, bool* is_dir) {
    if (in[0] == 0) {
        strcpy(out, ".");
        *outlen = 1;
        *is_dir = true;
        return MX_OK;
    }

    bool rooted = (in[0] == '/');
    size_t in_index = 0; // Index of the next byte to read
    size_t out_index = 0; // Index of the next byte to write

    if (rooted) {
        out[out_index++] = '/';
        in_index++;
        *is_dir = true;
    }
    size_t dotdot = out_index; // The output index at which '..' cannot be cleaned further.

    while (in[in_index] != 0) {
        *is_dir = true;
        if (in[in_index] == '/') {
            // 1. Reduce multiple slashes to a single slash
            CHECK_CAN_INCREMENT(in_index);
            in_index++;
        } else if (in[in_index] == '.' && IS_SEPARATOR(in[in_index + 1])) {
            // 2. Eliminate . path name elements (the current directory)
            CHECK_CAN_INCREMENT(in_index);
            in_index++;
        } else if (in[in_index] == '.' && in[in_index + 1] == '.' &&
                   IS_SEPARATOR(in[in_index + 2])) {
            CHECK_CAN_INCREMENT(in_index + 1);
            in_index += 2;
            if (out_index > dotdot) {
                // 3. Eliminate .. path elements (the parent directory) and the element that
                // precedes them.
                out_index--;
                while (out_index > dotdot && out[out_index] != '/') { out_index--; }
            } else if (rooted) {
                // 4. Eliminate .. elements that begin a rooted path, that is, replace /.. by / at
                // the beginning of a path.
                continue;
            } else if (!rooted) {
                if (out_index > 0) {
                    out[out_index++] = '/';
                }
                // 5. Leave intact .. elements that begin a non-rooted path.
                out[out_index++] = '.';
                out[out_index++] = '.';
                dotdot = out_index;
            }
        } else {
            *is_dir = false;
            if ((rooted && out_index != 1) || (!rooted && out_index != 0)) {
                // Add '/' before normal path component, for non-root components.
                out[out_index++] = '/';
            }

            while (!IS_SEPARATOR(in[in_index])) {
                CHECK_CAN_INCREMENT(in_index);
                out[out_index++] = in[in_index++];
            }
        }
    }

    if (out_index == 0) {
        strcpy(out, ".");
        *outlen = 1;
        *is_dir = true;
        return MX_OK;
    }

    // Append null character
    *outlen = out_index;
    out[out_index++] = 0;
    return MX_OK;
}

mx_status_t __mxio_open_at(mxio_t** io, int dirfd, const char* path, int flags, uint32_t mode) {
    if (path == NULL) {
        return MX_ERR_INVALID_ARGS;
    }
    if (path[0] == 0) {
        return MX_ERR_NOT_FOUND;
    }
    mxio_t* iodir = mxio_iodir(&path, dirfd);
    if (iodir == NULL) {
        return MX_ERR_BAD_HANDLE;
    }

    char clean[PATH_MAX];
    size_t outlen;
    bool is_dir;
    mx_status_t status = __mxio_cleanpath(path, clean, &outlen, &is_dir);
    if (status != MX_OK) {
        return status;
    }
    flags |= (is_dir ? O_DIRECTORY : 0);

    status = iodir->ops->open(iodir, clean, flags, mode, io);
    mxio_release(iodir);
    return status;
}

mx_status_t __mxio_open(mxio_t** io, const char* path, int flags, uint32_t mode) {
    return __mxio_open_at(io, AT_FDCWD, path, flags, mode);
}

static void update_cwd_path(const char* path) {
    if (path[0] == '/') {
        // it's "absolute", but we'll still parse it as relative (from /)
        // so that we normalize the path (resolving, ., .., //, etc)
        mxio_cwd_path[0] = '/';
        mxio_cwd_path[1] = 0;
        path++;
    }

    size_t seglen;
    const char* next;
    for (; path[0]; path = next) {
        next = strchr(path, '/');
        if (next == NULL) {
            seglen = strlen(path);
            next = path + seglen;
        } else {
            seglen = next - path;
            next++;
        }
        if (seglen == 0) {
            // empty segment, skip
            continue;
        }
        if ((seglen == 1) && (path[0] == '.')) {
            // no-change segment, skip
            continue;
        }
        if ((seglen == 2) && (path[0] == '.') && (path[1] == '.')) {
            // parent directory, remove the trailing path segment from cwd_path
            char* x = strrchr(mxio_cwd_path, '/');
            if (x == NULL) {
                // shouldn't ever happen
                goto wat;
            }
            // remove the current trailing path segment from cwd
            if (x == mxio_cwd_path) {
                // but never remove the first /
                mxio_cwd_path[1] = 0;
            } else {
                x[0] = 0;
            }
            continue;
        }
        // regular path segment, append to cwd_path
        size_t len = strlen(mxio_cwd_path);
        if ((len + seglen + 2) >= PATH_MAX) {
            // doesn't fit, shouldn't happen, but...
            goto wat;
        }
        if (len != 1) {
            // if len is 1, path is "/", so don't append a '/'
            mxio_cwd_path[len++] = '/';
        }
        memcpy(mxio_cwd_path + len, path, seglen);
        mxio_cwd_path[len + seglen] = 0;
    }
    return;

wat:
    strcpy(mxio_cwd_path, "(unknown)");
    return;
}

// Opens the directory containing path
//
// Returns the non-directory portion of the path in 'out', which
// must be a buffer that can fit [NAME_MAX + 1] characters.
static mx_status_t __mxio_opendir_containing_at(mxio_t** io, int dirfd, const char* path,
                                                char* out) {
    if (path == NULL) {
        return MX_ERR_INVALID_ARGS;
    }

    mxio_t* iodir = mxio_iodir(&path, dirfd);
    if (iodir == NULL) {
        return MX_ERR_BAD_HANDLE;
    }

    char clean[PATH_MAX];
    size_t pathlen;
    bool is_dir;
    mx_status_t status = __mxio_cleanpath(path, clean, &pathlen, &is_dir);
    if (status != MX_OK) {
        mxio_release(iodir);
        return status;
    }

    // Find the last '/'; copy everything after it.
    size_t i = 0;
    for (i = pathlen - 1; i > 0; i--) {
        if (clean[i] == '/') {
            clean[i] = 0;
            i++;
            break;
        }
    }

    // clean[i] is now the start of the name
    size_t namelen = pathlen - i;
    if (namelen + (is_dir ? 1 : 0) > NAME_MAX) {
        mxio_release(iodir);
        return MX_ERR_BAD_PATH;
    }

    // Copy the trailing 'name' to out.
    memcpy(out, clean + i, namelen);
    if (is_dir) {
        // TODO(smklein): Propagate this information without using
        // the output name; it'll simplify server-side path parsing
        // if all trailing slashes are replaced with "O_DIRECTORY".
        out[namelen++] = '/';
    }
    out[namelen] = 0;

    if (i == 0 && clean[i] != '/') {
        clean[0] = '.';
        clean[1] = 0;
    }

    mx_status_t r = iodir->ops->open(iodir, clean, O_RDONLY | O_DIRECTORY, 0, io);
    mxio_release(iodir);
    return r;
}

// 'name' must be a user-provided buffer, at least NAME_MAX + 1 bytes long.
static mx_status_t __mxio_opendir_containing(mxio_t** io, const char* path, char* name) {
    return __mxio_opendir_containing_at(io, AT_FDCWD, path, name);
}


// hook into libc process startup
// this is called prior to main to set up the mxio world
// and thus does not use the mxio_lock
void __libc_extensions_init(uint32_t handle_count,
                            mx_handle_t handle[],
                            uint32_t handle_info[],
                            uint32_t name_count,
                            char** names) {
    int stdio_fd = -1;

    // extract handles we care about
    for (uint32_t n = 0; n < handle_count; n++) {
        unsigned arg = PA_HND_ARG(handle_info[n]);
        mx_handle_t h = handle[n];

        // MXIO uses this bit as a flag to say
        // that an fd should be duped into 0/1/2
        // and become all of stdin/out/err
        if (arg & MXIO_FLAG_USE_FOR_STDIO) {
            arg &= (~MXIO_FLAG_USE_FOR_STDIO);
            if (arg < MAX_MXIO_FD) {
                stdio_fd = arg;
            }
        }

        switch (PA_HND_TYPE(handle_info[n])) {
        case PA_MXIO_CWD:
            mxio_cwd_handle = mxio_remote_create(h, 0);
            break;
        case PA_MXIO_REMOTE:
            // remote objects may have a second handle
            // which is for signaling events
            if (((n + 1) < handle_count) &&
                (handle_info[n] == handle_info[n + 1])) {
                mxio_fdtab[arg] = mxio_remote_create(h, handle[n + 1]);
                handle_info[n + 1] = 0;
            } else {
                mxio_fdtab[arg] = mxio_remote_create(h, 0);
            }
            mxio_fdtab[arg]->dupcount++;
            break;
        case PA_MXIO_PIPE:
            mxio_fdtab[arg] = mxio_pipe_create(h);
            mxio_fdtab[arg]->dupcount++;
            break;
        case PA_MXIO_LOGGER:
            mxio_fdtab[arg] = mxio_logger_create(h);
            mxio_fdtab[arg]->dupcount++;
            break;
        case PA_MXIO_SOCKET:
            // socket objects have a second handle
            if (((n + 1) < handle_count) &&
                (handle_info[n] == handle_info[n + 1])) {
                mxio_fdtab[arg] = mxio_socket_create(h, handle[n + 1], MXIO_FLAG_SOCKET_CONNECTED);
                handle_info[n + 1] = 0;
                mxio_fdtab[arg]->dupcount++;
            } else {
                mx_handle_close(h);
            }
            break;
        case PA_NS_DIR:
            // we always contine here to not steal the
            // handles from higher level code that may
            // also need access to the namespace
            if (arg >= name_count) {
                continue;
            }
            if (mxio_root_ns == NULL) {
                if (mxio_ns_create(&mxio_root_ns) < 0) {
                    continue;
                }
            }
            mxio_ns_bind(mxio_root_ns, names[arg], h);
            continue;
        default:
            // unknown handle, leave it alone
            continue;
        }
        handle[n] = 0;
        handle_info[n] = 0;
    }

    // Set up thread local storage for rchannels.
    __mxio_rchannel_init();

    // TODO(abarth): The cwd path string should be more tightly coupled with
    // the cwd handle.
    const char* cwd = getenv("PWD");
    if (cwd != NULL) {
        update_cwd_path(cwd);
    }

    mxio_t* use_for_stdio = (stdio_fd >= 0) ? mxio_fdtab[stdio_fd] : NULL;

    // configure stdin/out/err if not init'd
    for (uint32_t n = 0; n < 3; n++) {
        if (mxio_fdtab[n] == NULL) {
            if (use_for_stdio) {
                mxio_fdtab[n] = use_for_stdio;
            } else {
                mxio_fdtab[n] = mxio_null_create();
            }
            mxio_fdtab[n]->dupcount++;
        }
    }

    if (mxio_root_ns) {
        mxio_t* io = mxio_ns_open_root(mxio_root_ns);
        if (io != NULL) {
            // If we have a root from the legacy PA_MXIO_ROOT,
            // a specified root namespace overrides it
            if (mxio_root_handle) {
                mxio_close(mxio_root_handle);
            }
            mxio_root_handle = io;
        }
    }
    if (mxio_root_handle) {
        mxio_root_init = true;
        if(!mxio_cwd_handle) {
            __mxio_open(&mxio_cwd_handle, mxio_cwd_path, O_RDONLY | O_DIRECTORY, 0);
        }
    } else {
        // placeholder null handle
        mxio_root_handle = mxio_null_create();
    }
    if (mxio_cwd_handle == NULL) {
        mxio_cwd_handle = mxio_null_create();
    }

    atexit(mxio_exit);
}


mx_status_t mxio_ns_install(mxio_ns_t* ns) {
    mxio_t* io = mxio_ns_open_root(ns);
    if (io == NULL) {
        return MX_ERR_IO;
    }

    mxio_t* old_root = NULL;
    mx_status_t status;

    mtx_lock(&mxio_lock);
    if (mxio_root_ns != NULL) {
        //TODO: support replacing an active namespace
        status = MX_ERR_ALREADY_EXISTS;
    } else {
        if (mxio_root_handle) {
            old_root = mxio_root_handle;
        }
        mxio_root_handle = io;
        status = MX_OK;
    }
    mtx_unlock(&mxio_lock);

    if (old_root) {
        mxio_close(old_root);
        mxio_release(old_root);
    }
    return status;
}


mx_status_t mxio_clone_root(mx_handle_t* handles, uint32_t* types) {
    // The root handle is established in the init hook called from
    // libc startup (or, in the special case of devmgr, installed
    // slightly later), and is never NULL and does not change
    // in normal operation
    mx_status_t r = mxio_root_handle->ops->clone(mxio_root_handle, handles, types);
    if (r > 0) {
        *types = PA_MXIO_REMOTE;
    }
    return r;
}

mx_status_t mxio_clone_cwd(mx_handle_t* handles, uint32_t* types) {
    mx_status_t r = mxio_cwd_handle->ops->clone(mxio_cwd_handle, handles, types);
    if (r > 0) {
        *types = PA_MXIO_CWD;
    }
    return r;
}

mx_status_t mxio_clone_fd(int fd, int newfd, mx_handle_t* handles, uint32_t* types) {
    mx_status_t r;
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return MX_ERR_BAD_HANDLE;
    }
    // TODO(MG-973): implement/honor close-on-exec flag
    if ((r = io->ops->clone(io, handles, types)) > 0) {
        for (int i = 0; i < r; i++) {
            types[i] |= (newfd << 16);
        }
    }
    mxio_release(io);
    return r;
}

mx_status_t mxio_transfer_fd(int fd, int newfd, mx_handle_t* handles, uint32_t* types) {
    mxio_t* io;
    mx_status_t status;
    if ((status = mxio_unbind_from_fd(fd, &io)) < 0) {
        return status;
    }
    if ((status = io->ops->unwrap(io, handles, types)) < 0) {
        return status;
    }
    for (int n = 0; n < status; n++) {
        types[n] |= (newfd << 16);
    }
    return status;
}

ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return MX_ERR_BAD_HANDLE;
    }
    ssize_t r = io->ops->ioctl(io, op, in_buf, in_len, out_buf, out_len);
    mxio_release(io);
    return r;
}

mx_status_t mxio_wait(mxio_t* io, uint32_t events, mx_time_t deadline,
                      uint32_t* out_pending) {
    mx_handle_t h = MX_HANDLE_INVALID;
    mx_signals_t signals = 0;
    io->ops->wait_begin(io, events, &h, &signals);
    if (h == MX_HANDLE_INVALID)
        // Wait operation is not applicable to the handle.
        return MX_ERR_INVALID_ARGS;

    mx_signals_t pending;
    mx_status_t status = mx_object_wait_one(h, signals, deadline, &pending);
    if (status == MX_OK || status == MX_ERR_TIMED_OUT) {
        io->ops->wait_end(io, pending, &events);
        if (out_pending != NULL)
            *out_pending = events;
    }

    return status;
}

mx_status_t mxio_wait_fd(int fd, uint32_t events, uint32_t* _pending, mx_time_t deadline) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL)
        return MX_ERR_BAD_HANDLE;

    mx_status_t status = mxio_wait(io, events, deadline, _pending);

    mxio_release(io);
    return status;
}

int mxio_stat(mxio_t* io, struct stat* s) {
    vnattr_t attr;
    int r = io->ops->misc(io, MXRIO_STAT, 0, sizeof(attr), &attr, 0);
    if (r < 0) {
        return MX_ERR_BAD_HANDLE;
    }
    if (r < (int)sizeof(attr)) {
        return MX_ERR_IO;
    }
    memset(s, 0, sizeof(struct stat));
    s->st_mode = attr.mode;
    s->st_ino = attr.inode;
    s->st_size = attr.size;
    s->st_blksize = attr.blksize;
    s->st_blocks = attr.blkcount;
    s->st_nlink = attr.nlink;
    s->st_ctim.tv_sec = attr.create_time / MX_SEC(1);
    s->st_ctim.tv_nsec = attr.create_time % MX_SEC(1);
    s->st_mtim.tv_sec = attr.modify_time / MX_SEC(1);
    s->st_mtim.tv_nsec = attr.modify_time % MX_SEC(1);
    return 0;
}


mx_status_t mxio_setattr(mxio_t* io, vnattr_t* vn){
    mx_status_t r = io->ops->misc(io, MXRIO_SETATTR, 0, 0, vn, sizeof(*vn));
    if (r < 0) {
        return MX_ERR_BAD_HANDLE;
    }

    return  r;
}


// TODO(MG-974): determine complete correct mapping
int mxio_status_to_errno(mx_status_t status) {
    switch (status) {
    case MX_ERR_NOT_FOUND: return ENOENT;
    case MX_ERR_NO_MEMORY: return ENOMEM;
    case MX_ERR_INVALID_ARGS: return EINVAL;
    case MX_ERR_BUFFER_TOO_SMALL: return EINVAL;
    case MX_ERR_TIMED_OUT: return ETIMEDOUT;
    case MX_ERR_UNAVAILABLE: return EBUSY;
    case MX_ERR_ALREADY_EXISTS: return EEXIST;
    case MX_ERR_PEER_CLOSED: return EPIPE;
    case MX_ERR_BAD_STATE: return EPIPE;
    case MX_ERR_BAD_PATH: return ENAMETOOLONG;
    case MX_ERR_IO: return EIO;
    case MX_ERR_NOT_FILE: return EISDIR;
    case MX_ERR_NOT_DIR: return ENOTDIR;
    case MX_ERR_NOT_SUPPORTED: return ENOTSUP;
    case MX_ERR_OUT_OF_RANGE: return EINVAL;
    case MX_ERR_NO_RESOURCES: return ENOMEM;
    case MX_ERR_BAD_HANDLE: return EBADF;
    case MX_ERR_ACCESS_DENIED: return EACCES;
    case MX_ERR_SHOULD_WAIT: return EAGAIN;
    case MX_ERR_FILE_BIG: return EFBIG;
    case MX_ERR_NO_SPACE: return ENOSPC;
    case MX_ERR_NOT_EMPTY: return ENOTEMPTY;
    case MX_ERR_IO_REFUSED: return ECONNREFUSED;
    case MX_ERR_CANCELED: return ECANCELED;
    case MX_ERR_PROTOCOL_NOT_SUPPORTED: return EPROTONOSUPPORT;
    case MX_ERR_ADDRESS_UNREACHABLE: return ENETUNREACH;
    case MX_ERR_ADDRESS_IN_USE: return EADDRINUSE;
    case MX_ERR_NOT_CONNECTED: return ENOTCONN;
    case MX_ERR_CONNECTION_REFUSED: return ECONNREFUSED;
    case MX_ERR_CONNECTION_RESET: return ECONNRESET;
    case MX_ERR_CONNECTION_ABORTED: return ECONNABORTED;

    // No specific translation, so return a generic errno value.
    default: return EIO;
    }
}

// The functions from here on provide implementations of fd and path
// centric posix-y io operations.

ssize_t readv(int fd, const struct iovec* iov, int num) {
    ssize_t count = 0;
    ssize_t r;
    while (num > 0) {
        if (iov->iov_len != 0) {
            r = read(fd, iov->iov_base, iov->iov_len);
            if (r < 0) {
                return count ? count : r;
            }
            if ((size_t)r < iov->iov_len) {
                return count + r;
            }
            count += r;
        }
        iov++;
        num--;
    }
    return count;
}

ssize_t writev(int fd, const struct iovec* iov, int num) {
    ssize_t count = 0;
    ssize_t r;
    while (num > 0) {
        if (iov->iov_len != 0) {
            r = write(fd, iov->iov_base, iov->iov_len);
            if (r < 0) {
                return count ? count : r;
            }
            if ((size_t)r < iov->iov_len) {
                return count + r;
            }
            count += r;
        }
        iov++;
        num--;
    }
    return count;
}

mx_status_t _mmap_file(size_t offset, size_t len, uint32_t mx_flags, int flags, int fd,
                       off_t fd_off, uintptr_t* out) {
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return MX_ERR_BAD_HANDLE;
    }

    // At the moment, these parameters are sent to filesystem servers purely
    // for validation, since there is no mechanism to create a "subset vmo"
    // from the original VMO.
    // TODO(smklein): Once (if?) we can create 'subset' vmos, remove the
    // fd_off argument to mx_vmar_map below.
    mxrio_mmap_data_t data;
    data.offset = fd_off;
    data.length = len;
    data.flags = mx_flags | (flags & MAP_PRIVATE ? MXIO_MMAP_FLAG_PRIVATE : 0);

    mx_status_t r = io->ops->misc(io, MXRIO_MMAP, 0, sizeof(data), &data, sizeof(data));
    mxio_release(io);
    if (r < 0) {
        return r;
    }
    mx_handle_t vmo = r;

    uintptr_t ptr = 0;
    r = mx_vmar_map(mx_vmar_root_self(), offset, vmo, data.offset, data.length, mx_flags, &ptr);
    mx_handle_close(vmo);
    // TODO: map this as shared if we ever implement forking
    if (r < 0) {
        return r;
    }

    *out = ptr;
    return MX_OK;
}

int unlinkat(int dirfd, const char* path, int flags) {
    char name[NAME_MAX + 1];
    mxio_t* io;
    mx_status_t r;
    if ((r = __mxio_opendir_containing_at(&io, dirfd, path, name)) < 0) {
        return ERROR(r);
    }
    r = io->ops->misc(io, MXRIO_UNLINK, 0, 0, (void*)name, strlen(name));
    io->ops->close(io);
    mxio_release(io);
    return STATUS(r);
}

ssize_t read(int fd, void* buf, size_t count) {
    if (buf == NULL && count > 0) {
        return ERRNO(EINVAL);
    }

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    mx_status_t status;
    for (;;) {
        status = io->ops->read(io, buf, count);
        if (status != MX_ERR_SHOULD_WAIT || io->flags & MXIO_FLAG_NONBLOCK) {
            break;
        }
        mxio_wait_fd(fd, MXIO_EVT_READABLE | MXIO_EVT_PEER_CLOSED, NULL, MX_TIME_INFINITE);
    }
    mxio_release(io);
    return status < 0 ? STATUS(status) : status;
}

ssize_t write(int fd, const void* buf, size_t count) {
    if (buf == NULL && count > 0) {
        return ERRNO(EINVAL);
    }

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    mx_status_t status;
    for (;;) {
        status = io->ops->write(io, buf, count);
        if (status != MX_ERR_SHOULD_WAIT || io->flags & MXIO_FLAG_NONBLOCK) {
            break;
        }
        mxio_wait_fd(fd, MXIO_EVT_WRITABLE | MXIO_EVT_PEER_CLOSED, NULL, MX_TIME_INFINITE);
    }
    mxio_release(io);
    return status < 0 ? STATUS(status) : status;
}

ssize_t preadv(int fd, const struct iovec* iov, int count, off_t ofs) {
    ssize_t iov_count = 0;
    ssize_t r;
    while (count > 0) {
        if (iov->iov_len != 0) {
            r = pread(fd, iov->iov_base, iov->iov_len, ofs);
            if (r < 0) {
                return iov_count ? iov_count : r;
            }
            if ((size_t)r < iov->iov_len) {
                return iov_count + r;
            }
            iov_count += r;
            ofs += r;
        }
        iov++;
        count--;
    }
    return iov_count;
}

ssize_t pread(int fd, void* buf, size_t size, off_t ofs) {
    if (buf == NULL && size > 0) {
        return ERRNO(EINVAL);
    }

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    mx_status_t status;
    for (;;) {
        status = io->ops->read_at(io, buf, size, ofs);
        if (status != MX_ERR_SHOULD_WAIT || io->flags & MXIO_FLAG_NONBLOCK) {
            break;
        }
        mxio_wait_fd(fd, MXIO_EVT_READABLE | MXIO_EVT_PEER_CLOSED, NULL, MX_TIME_INFINITE);
    }
    mxio_release(io);
    return status < 0 ? STATUS(status) : status;
}

ssize_t pwritev(int fd, const struct iovec* iov, int count, off_t ofs) {
    ssize_t iov_count = 0;
    ssize_t r;
    while (count > 0) {
        if (iov->iov_len != 0) {
            r = pwrite(fd, iov->iov_base, iov->iov_len, ofs);
            if (r < 0) {
                return iov_count ? iov_count : r;
            }
            if ((size_t)r < iov->iov_len) {
                return iov_count + r;
            }
            iov_count += r;
            ofs += r;
        }
        iov++;
        count--;
    }
    return iov_count;
}

ssize_t pwrite(int fd, const void* buf, size_t size, off_t ofs) {
    if (buf == NULL && size > 0) {
        return ERRNO(EINVAL);
    }

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    mx_status_t status;
    for (;;) {
        status = io->ops->write_at(io, buf, size, ofs);
        if (status != MX_ERR_SHOULD_WAIT || io->flags & MXIO_FLAG_NONBLOCK) {
            break;
        }
        mxio_wait_fd(fd, MXIO_EVT_WRITABLE | MXIO_EVT_PEER_CLOSED, NULL, MX_TIME_INFINITE);
    }
    mxio_release(io);
    return status < 0 ? STATUS(status) : status;
}

int close(int fd) {
    mtx_lock(&mxio_lock);
    if ((fd < 0) || (fd >= MAX_MXIO_FD) || (mxio_fdtab[fd] == NULL)) {
        mtx_unlock(&mxio_lock);
        return ERRNO(EBADF);
    }
    mxio_t* io = mxio_fdtab[fd];
    io->dupcount--;
    mxio_fdtab[fd] = NULL;
    if (io->dupcount > 0) {
        // still alive in other fdtab slots
        mtx_unlock(&mxio_lock);
        mxio_release(io);
        return MX_OK;
    } else {
        mtx_unlock(&mxio_lock);
        int r = io->ops->close(io);
        mxio_release(io);
        return STATUS(r);
    }
}

static int mxio_dup(int oldfd, int newfd, int starting_fd) {
    mxio_t* io = fd_to_io(oldfd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    int fd = mxio_bind_to_fd(io, newfd, starting_fd);
    if (fd < 0) {
        mxio_release(io);
    }
    return fd;
}

int dup2(int oldfd, int newfd) {
    return mxio_dup(oldfd, newfd, 0);
}

int dup(int oldfd) {
    return mxio_dup(oldfd, -1, 0);
}

int dup3(int oldfd, int newfd, int flags) {
    // dup3 differs from dup2 in that it fails with EINVAL, rather
    // than being a no op, on being given the same fd for both old and
    // new.
    if (oldfd == newfd) {
        return ERRNO(EINVAL);
    }

    if (flags != 0 && flags != O_CLOEXEC) {
        return ERRNO(EINVAL);
    }

    // TODO(MG-973) Implement O_CLOEXEC.
    return mxio_dup(oldfd, newfd, 0);
}

int fcntl(int fd, int cmd, ...) {
// Note that it is not safe to pull out the int out of the
// variadic arguments at the top level, as callers are not
// required to pass anything for many of the commands.
#define GET_INT_ARG(ARG)         \
    va_list args;                \
    va_start(args, cmd);         \
    int ARG = va_arg(args, int); \
    va_end(args)

    switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
        // TODO(MG-973) Implement CLOEXEC.
        GET_INT_ARG(starting_fd);
        return mxio_dup(fd, -1, starting_fd);
    }
    case F_GETFD: {
        mxio_t* io = fd_to_io(fd);
        if (io == NULL) {
            return ERRNO(EBADF);
        }
        int flags = (int)(io->flags & MXIO_FD_FLAGS);
        // POSIX mandates that the return value be nonnegative if successful.
        assert(flags >= 0);
        mxio_release(io);
        return flags;
    }
    case F_SETFD: {
        mxio_t* io = fd_to_io(fd);
        if (io == NULL) {
            return ERRNO(EBADF);
        }
        GET_INT_ARG(flags);
        // TODO(MG-973) Implement CLOEXEC.
        io->flags &= ~MXIO_FD_FLAGS;
        io->flags |= (int32_t)flags & MXIO_FD_FLAGS;
        mxio_release(io);
        return 0;
    }
    case F_GETFL: {
        mxio_t* io = fd_to_io(fd);
        if (io == NULL) {
            return ERRNO(EBADF);
        }
        uint32_t flags = 0;
        mx_status_t r = io->ops->misc(io, MXRIO_FCNTL, 0, F_GETFL, &flags, 0);
        if (r == MX_ERR_NOT_SUPPORTED) {
            // We treat this as non-fatal, as it's valid for a remote to
            // simply not support FCNTL, but we still want to correctly
            // report the state of the (local) NONBLOCK flag
            flags = 0;
            r = MX_OK;
        }
        if (io->flags & MXIO_FLAG_NONBLOCK) {
            flags |= O_NONBLOCK;
        }
        mxio_release(io);
        if (r < 0) {
            return STATUS(r);
        }
        return flags;
    }
    case F_SETFL: {
        mxio_t* io = fd_to_io(fd);
        if (io == NULL) {
            return ERRNO(EBADF);
        }
        GET_INT_ARG(n);

        mx_status_t r;
        if (n == O_NONBLOCK) {
            // NONBLOCK is local, so we can avoid the rpc for it
            // which is good in situations where the remote doesn't
            // support FCNTL but it's still valid to set non-blocking
            r = MX_OK;
        } else {
            r = io->ops->misc(io, MXRIO_FCNTL, n & (~O_NONBLOCK), F_SETFL, NULL, 0);
        }
        if (r != MX_OK) {
            n = STATUS(r);
        } else {
            if (n & O_NONBLOCK) {
                io->flags |= MXIO_FLAG_NONBLOCK;
            } else {
                io->flags &= ~MXIO_FLAG_NONBLOCK;
            }
            n = 0;
        }
        mxio_release(io);
        return n;
    }
    case F_GETOWN:
    case F_SETOWN:
        // TODO(kulakowski) Socket support.
        return ERRNO(ENOSYS);
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
        // TODO(kulakowski) Advisory file locking support.
        return ERRNO(ENOSYS);
    default:
        return ERRNO(EINVAL);
    }

#undef GET_INT_ARG
}

off_t lseek(int fd, off_t offset, int whence) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    off_t r = io->ops->seek(io, offset, whence);
    if (r == MX_ERR_WRONG_TYPE) {
        // Although 'ESPIPE' is a bit of a misnomer, it is the valid errno
        // for any fd which does not implement seeking (i.e., for pipes,
        // sockets, etc).
        errno = ESPIPE;
        r = -1;
    } else if (r < 0) {
        r = ERROR(r);
    }
    mxio_release(io);
    return r;
}

static int getdirents(int fd, void* ptr, size_t len, long cmd) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    int r = STATUS(io->ops->misc(io, MXRIO_READDIR, cmd, len, ptr, 0));
    mxio_release(io);
    return r;
}

static int truncateat(int dirfd, const char* path, off_t len) {
    mxio_t* io;
    mx_status_t r;

    if ((r = __mxio_open_at(&io, dirfd, path, O_WRONLY, 0)) < 0) {
        return ERROR(r);
    }
    r = io->ops->misc(io, MXRIO_TRUNCATE, len, 0, NULL, 0);
    mxio_close(io);
    mxio_release(io);
    return STATUS(r);
}

int truncate(const char* path, off_t len) {
    return truncateat(AT_FDCWD, path, len);
}

int ftruncate(int fd, off_t len) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    int r = STATUS(io->ops->misc(io, MXRIO_TRUNCATE, len, 0, NULL, 0));
     mxio_release(io);
     return r;
}

// Filesystem operations (such as rename and link) which act on multiple paths
// have some additional complexity on Magenta. These operations (eventually) act
// on two pairs of variables: a source parent vnode + name, and a target parent
// vnode + name. However, the loose coupling of these pairs can make their
// correspondence difficult, especially when accessing each parent vnode may
// involve crossing various filesystem boundaries.
//
// To resolve this problem, these kinds of operations involve:
// - Opening the source parent vnode directly.
// - Opening the target parent vnode directly, + acquiring a "vnode token".
// - Sending the real operation + names to the source parent vnode, along with
//   the "vnode token" representing the target parent vnode.
//
// Using magenta kernel primitives (cookies) to authenticate the vnode token, this
// allows these multi-path operations to mix absolute / relative paths and cross
// mount points with ease.
static int two_path_op_at(uint32_t op, int olddirfd, const char* oldpath,
                          int newdirfd, const char* newpath) {
    char oldname[NAME_MAX + 1];
    mxio_t* io_oldparent;
    mx_status_t status = MX_OK;
    if ((status = __mxio_opendir_containing_at(&io_oldparent, olddirfd, oldpath, oldname)) < 0) {
        return ERROR(status);
    }

    char newname[NAME_MAX + 1];
    mxio_t* io_newparent;
    if ((status = __mxio_opendir_containing_at(&io_newparent, newdirfd, newpath, newname)) < 0) {
        goto oldparent_open;
    }

    mx_handle_t token;
    status = io_newparent->ops->ioctl(io_newparent, IOCTL_VFS_GET_TOKEN,
                                      NULL, 0, &token, sizeof(token));
    if (status < 0) {
        goto newparent_open;
    }

    char name[MXIO_CHUNK_SIZE];
    size_t oldlen = strlen(oldname);
    size_t newlen = strlen(newname);
    static_assert(sizeof(oldname) + sizeof(newname) + 2 < sizeof(name),
                  "Dual-path operation names should fit in MXIO name buffer");
    memcpy(name, oldname, oldlen);
    name[oldlen] = '\0';
    memcpy(name + oldlen + 1, newname, newlen);
    name[oldlen + newlen + 1] = '\0';
    status = io_oldparent->ops->misc(io_oldparent, op, token, 0,
                                     (void*)name, oldlen + newlen + 2);
    goto newparent_open;
newparent_open:
    io_newparent->ops->close(io_newparent);
    mxio_release(io_newparent);
oldparent_open:
    io_oldparent->ops->close(io_oldparent);
    mxio_release(io_oldparent);
    return STATUS(status);
}

int renameat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath) {
    return two_path_op_at(MXRIO_RENAME, olddirfd, oldpath, newdirfd, newpath);
}

int rename(const char* oldpath, const char* newpath) {
    return two_path_op_at(MXRIO_RENAME, AT_FDCWD, oldpath, AT_FDCWD, newpath);
}

int link(const char* oldpath, const char* newpath) {
    return two_path_op_at(MXRIO_LINK, AT_FDCWD, oldpath, AT_FDCWD, newpath);
}

int unlink(const char* path) {
    return unlinkat(AT_FDCWD, path, 0);
}

static int vopenat(int dirfd, const char* path, int flags, va_list args) {
    mxio_t* io = NULL;
    mx_status_t r;
    int fd;
    uint32_t mode = 0;

    if (flags & O_CREAT) {
        if (flags & O_DIRECTORY) {
            // The behavior of open with O_CREAT | O_DIRECTORY is underspecified
            // in POSIX. To help avoid programmer error, we explicitly disallow
            // the combination.
            return ERRNO(EINVAL);
        }
        mode = va_arg(args, uint32_t) & 0777;
    }
    if ((r = __mxio_open_at(&io, dirfd, path, flags, mode)) < 0) {
        return ERROR(r);
    }
    if (flags & O_NONBLOCK) {
        io->flags |= MXIO_FLAG_NONBLOCK;
    }
    if ((fd = mxio_bind_to_fd(io, -1, 0)) < 0) {
        io->ops->close(io);
        mxio_release(io);
        return ERRNO(EMFILE);
    }
    return fd;
}

int open(const char* path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    int ret = vopenat(AT_FDCWD, path, flags, ap);
    va_end(ap);
    return ret;
}

int openat(int dirfd, const char* path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    int ret = vopenat(dirfd, path, flags, ap);
    va_end(ap);
    return ret;
}

int mkdir(const char* path, mode_t mode) {
    return mkdirat(AT_FDCWD, path, mode);
}

int mkdirat(int dirfd, const char* path, mode_t mode) {
    mxio_t* io = NULL;
    mx_status_t r;

    mode = (mode & 0777) | S_IFDIR;

    if ((r = __mxio_open_at(&io, dirfd, path, O_RDONLY | O_CREAT | O_EXCL, mode)) < 0) {
        return ERROR(r);
    }
    io->ops->close(io);
    mxio_release(io);
    return 0;
}

int fsync(int fd) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    int r = STATUS(io->ops->misc(io, MXRIO_SYNC, 0, 0, 0, 0));
    mxio_release(io);
    return r;
}

int fdatasync(int fd) {
    // TODO(smklein): fdatasync does not need to flush metadata under certain
    // circumstances -- however, for now, this implementation will appear
    // functionally the same (if a little slower).
    return fsync(fd);
}

int fstat(int fd, struct stat* s) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    int r = STATUS(mxio_stat(io, s));
    mxio_release(io);
    return r;
}

int fstatat(int dirfd, const char* fn, struct stat* s, int flags) {
    mxio_t* io;
    mx_status_t r;

    if ((r = __mxio_open_at(&io, dirfd, fn, 0, 0)) < 0) {
        return ERROR(r);
    }
    r = mxio_stat(io, s);
    mxio_close(io);
    mxio_release(io);
    return STATUS(r);
}

int stat(const char* fn, struct stat* s) {
    return fstatat(AT_FDCWD, fn, s, 0);
}

int lstat(const char* path, struct stat* buf) {
    return stat(path, buf);
}

char* realpath(const char* restrict filename, char* restrict resolved) {
    ssize_t r;
    struct stat st;
    char tmp[PATH_MAX];
    size_t outlen;
    bool is_dir;

    if (!filename) {
        errno = EINVAL;
        return NULL;
    }

    if (filename[0] != '/') {
        // Convert 'filename' from a relative path to an absolute path.
        size_t file_len = strlen(filename);
        mtx_lock(&mxio_cwd_lock);
        size_t cwd_len = strlen(mxio_cwd_path);
        if (cwd_len + 1 + file_len >= PATH_MAX) {
            mtx_unlock(&mxio_cwd_lock);
            errno = ENAMETOOLONG;
            return NULL;
        }
        char tmp2[PATH_MAX];
        memcpy(tmp2, mxio_cwd_path, cwd_len);
        mtx_unlock(&mxio_cwd_lock);
        tmp2[cwd_len] = '/';
        strcpy(tmp2 + cwd_len + 1, filename);
        mx_status_t status = __mxio_cleanpath(tmp2, tmp, &outlen, &is_dir);
        if (status != MX_OK) {
            errno = EINVAL;
            return NULL;
        }
    } else {
        // Clean the provided absolute path
        mx_status_t status = __mxio_cleanpath(filename, tmp, &outlen, &is_dir);
        if (status != MX_OK) {
            errno = EINVAL;
            return NULL;
        }

        r = stat(tmp, &st);
        if (r < 0) {
            return NULL;
        }
    }
    return resolved ? strcpy(resolved, tmp) : strdup(tmp);
}

static int mx_utimens(mxio_t* io, const struct timespec times[2], int flags) {
    vnattr_t vn;
    mx_status_t r;

    vn.valid = 0;

    // extract modify time
    vn.modify_time = (times == NULL || times[1].tv_nsec == UTIME_NOW)
        ? mx_time_get(MX_CLOCK_UTC)
        : MX_SEC(times[1].tv_sec) + times[1].tv_nsec;

    if (times == NULL || times[1].tv_nsec != UTIME_OMIT) {
        // TODO(orr) UTIME_NOW requires write access or euid == owner or "appropriate privilege"
        vn.valid = ATTR_MTIME;      // for setattr, tell which fields are valid
    }

    // TODO(orr): access time not implemented for now

    // set time(s) on underlying object
    r = mxio_setattr(io, &vn);
    return r;
}

int utimensat(int dirfd, const char *fn,
              const struct timespec times[2], int flags) {
    mxio_t* io;
    mx_status_t r;

    // TODO(orr): AT_SYMLINK_NOFOLLOW
    if ((flags & AT_SYMLINK_NOFOLLOW) != 0) {
        // Allow this flag - don't return an error.  Fuchsia does not support
        // symlinks, so don't break utilities (like tar) that use this flag.
    }

    if ((r = __mxio_open_at(&io, dirfd, fn, 0, 0)) < 0) {
        return ERROR(r);
    }

    r = mx_utimens(io, times, 0);

    mxio_close(io);
    mxio_release(io);
    return STATUS(r);
}

int futimens(int fd, const struct timespec times[2]) {
    mxio_t* io = fd_to_io(fd);

    mx_status_t r = mx_utimens(io, times, 0);
    return STATUS(r);
}

int pipe2(int pipefd[2], int flags) {
    const int allowed_flags = O_NONBLOCK | O_CLOEXEC;
    if (flags & ~allowed_flags) {
        return ERRNO(EINVAL);
    }
    mxio_t *a, *b;
    int r = mxio_pipe_pair(&a, &b);
    if (r < 0) {
        return ERROR(r);
    }
    pipefd[0] = mxio_bind_to_fd(a, -1, 0);
    if (pipefd[0] < 0) {
        mxio_close(a);
        mxio_release(a);
        mxio_close(b);
        mxio_release(b);
        return ERROR(pipefd[0]);
    }
    pipefd[1] = mxio_bind_to_fd(b, -1, 0);
    if (pipefd[1] < 0) {
        close(pipefd[0]);
        mxio_close(b);
        mxio_release(b);
        return ERROR(pipefd[1]);
    }
    return 0;
}

int pipe(int pipefd[2]) {
    return pipe2(pipefd, 0);
}

int faccessat(int dirfd, const char* filename, int amode, int flag) {
    // For now, we just check to see if the file exists, until we
    // model permissions. But first, check that the flags and amode
    // are valid.
    const int allowed_flags = AT_EACCESS;
    if (flag & (~allowed_flags)) {
        return ERRNO(EINVAL);
    }

    // amode is allowed to be either a subset of this mask, or just F_OK.
    const int allowed_modes = R_OK | W_OK | X_OK;
    if (amode != F_OK && (amode & (~allowed_modes))) {
        return ERRNO(EINVAL);
    }

    // Since we are not tracking permissions yet, just check that the
    // file exists a la fstatat.
    mxio_t* io;
    mx_status_t status;
    if ((status = __mxio_open_at(&io, dirfd, filename, 0, 0)) < 0) {
        return ERROR(status);
    }
    struct stat s;
    status = mxio_stat(io, &s);
    mxio_close(io);
    mxio_release(io);
    return STATUS(status);
}

char* getcwd(char* buf, size_t size) {
    char tmp[PATH_MAX];
    if (buf == NULL) {
        buf = tmp;
        size = PATH_MAX;
    } else if (size == 0) {
        errno = EINVAL;
        return NULL;
    }

    char* out = NULL;
    mtx_lock(&mxio_cwd_lock);
    size_t len = strlen(mxio_cwd_path) + 1;
    if (len < size) {
        memcpy(buf, mxio_cwd_path, len);
        out = buf;
    } else {
        errno = ERANGE;
    }
    mtx_unlock(&mxio_cwd_lock);

    if (out == tmp) {
        out = strdup(tmp);
    }
    return out;
}

void mxio_chdir(mxio_t* io, const char* path) {
    mtx_lock(&mxio_cwd_lock);
    update_cwd_path(path);
    mtx_lock(&mxio_lock);
    mxio_t* old = mxio_cwd_handle;
    mxio_cwd_handle = io;
    old->ops->close(old);
    mxio_release(old);
    mtx_unlock(&mxio_lock);
    mtx_unlock(&mxio_cwd_lock);
}

int chdir(const char* path) {
    mxio_t* io;
    mx_status_t r;
    if ((r = __mxio_open(&io, path, O_RDONLY | O_DIRECTORY, 0)) < 0) {
        return STATUS(r);
    }
    mxio_chdir(io, path);
    return 0;
}

#define DIR_BUFSIZE 2048

struct __dirstream {
    mtx_t lock;
    int fd;
    // Total size of 'data' which has been filled with dirents
    size_t size;
    // Offset into 'data' of next ptr. NULL to reset the
    // directory lazily on the next call to getdirents
    uint8_t* ptr;
    // Internal cache of dirents
    uint8_t data[DIR_BUFSIZE];
    // Buffer returned to user
    struct dirent de;
};

static DIR* internal_opendir(int fd) {
    DIR* dir = calloc(1, sizeof(*dir));
    if (dir != NULL) {
        mtx_init(&dir->lock, mtx_plain);
        dir->size = 0;
        dir->fd = fd;
    }
    return dir;
}

DIR* opendir(const char* name) {
    int fd = open(name, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return NULL;
    DIR* dir = internal_opendir(fd);
    if (dir == NULL)
        close(fd);
    return dir;
}

DIR* fdopendir(int fd) {
    // Check the fd for validity, but we'll just store the fd
    // number so we don't save the mxio_t pointer.
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        errno = EBADF;
        return NULL;
    }
    // TODO(mcgrathr): Technically this should verify that it's
    // really a directory and fail with ENOTDIR if not.  But
    // that's not so easy to do, so don't bother for now.
    mxio_release(io);
    return internal_opendir(fd);
}

int closedir(DIR* dir) {
    close(dir->fd);
    free(dir);
    return 0;
}

struct dirent* readdir(DIR* dir) {
    mtx_lock(&dir->lock);
    struct dirent* de = &dir->de;
    for (;;) {
        if (dir->size >= sizeof(vdirent_t)) {
            vdirent_t* vde = (void*)dir->ptr;
            if (dir->size >= vde->size) {
                dir->ptr += vde->size;
                dir->size -= vde->size;
                if (vde->name[0]) {
                    de->d_ino = 0;
                    de->d_off = 0;
                    de->d_reclen = 0;
                    de->d_type = vde->type;
                    strcpy(de->d_name, vde->name);
                    break;
                } else {
                    // skip nameless entries.
                    // (they may be generated by filtering filesystems)
                    continue;
                }
            }
            dir->size = 0;
        }
        int64_t cmd = (dir->ptr == NULL) ? READDIR_CMD_RESET : READDIR_CMD_NONE;
        int r = getdirents(dir->fd, dir->data, DIR_BUFSIZE, cmd);
        if (r > 0) {
            dir->ptr = dir->data;
            dir->size = r;
            continue;
        }
        de = NULL;
        break;
    }
    mtx_unlock(&dir->lock);
    return de;
}

void rewinddir(DIR* dir) {
    mtx_lock(&dir->lock);
    dir->size = 0;
    dir->ptr = NULL;
    mtx_unlock(&dir->lock);
}

int dirfd(DIR* dir) {
    return dir->fd;
}

int isatty(int fd) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        errno = EBADF;
        return 0;
    }

    int ret;
    // TODO(MG-972)
    // For now, stdout etc. needs to be a tty for line buffering to
    // work. So let's pretend those are ttys but nothing else is.
    if (fd == 0 || fd == 1 || fd == 2) {
        ret = 1;
    } else {
        ret = 0;
        errno = ENOTTY;
    }

    mxio_release(io);

    return ret;
}

mode_t umask(mode_t mask) {
    mode_t oldmask;
    mtx_lock(&mxio_lock);
    oldmask = __mxio_global_state.umask;
    __mxio_global_state.umask = mask & 0777;
    mtx_unlock(&mxio_lock);
    return oldmask;
}

int mxio_handle_fd(mx_handle_t h, mx_signals_t signals_in, mx_signals_t signals_out,
                   bool shared_handle) {
    mxio_t* io = mxio_waitable_create(h, signals_in, signals_out, shared_handle);
    int fd = mxio_bind_to_fd(io, -1, 0);
    if (fd < 0) {
        mxio_close(io);
        mxio_release(io);
    }
    return fd;
}

// from mxio/private.h, to support message-loop integration

void __mxio_wait_begin(mxio_t* io, uint32_t events,
                       mx_handle_t* handle_out, mx_signals_t* signals_out) {
    return io->ops->wait_begin(io, events, handle_out, signals_out);
}

void __mxio_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* events_out) {
    return io->ops->wait_end(io, signals, events_out);
}

void __mxio_release(mxio_t* io) {
    mxio_release(io);
}


// TODO: getrlimit(RLIMIT_NOFILE, ...)
#define MAX_POLL_NFDS 1024

int poll(struct pollfd* fds, nfds_t n, int timeout) {
    if (n > MAX_POLL_NFDS) {
        return ERRNO(EINVAL);
    }

    mxio_t* ios[n];
    int ios_used_max = -1;

    mx_status_t r = MX_OK;
    nfds_t nvalid = 0;

    mx_wait_item_t items[n];

    for (nfds_t i = 0; i < n; i++) {
        struct pollfd* pfd = &fds[i];
        pfd->revents = 0; // initialize to zero

        ios[i] = NULL;
        if (pfd->fd < 0) {
            // if fd is negative, the entry is invalid
            continue;
        }
        mxio_t* io;
        if ((io = fd_to_io(pfd->fd)) == NULL) {
            // fd is not opened
            pfd->revents = POLLNVAL;
            continue;
        }
        ios[i] = io;
        ios_used_max = i;

        mx_handle_t h;
        mx_signals_t sigs;
        io->ops->wait_begin(io, pfd->events, &h, &sigs);
        if (h == MX_HANDLE_INVALID) {
            // wait operation is not applicable to the handle
            r = MX_ERR_INVALID_ARGS;
            break;
        }
        items[nvalid].handle = h;
        items[nvalid].waitfor = sigs;
        items[nvalid].pending = 0;
        nvalid++;
    }

    int nfds = 0;
    if (r == MX_OK && nvalid > 0) {
        mx_time_t tmo = (timeout >= 0) ? mx_deadline_after(MX_MSEC(timeout)) : MX_TIME_INFINITE;
        r = mx_object_wait_many(items, nvalid, tmo);
        // pending signals could be reported on MX_ERR_TIMED_OUT case as well
        if (r == MX_OK || r == MX_ERR_TIMED_OUT) {
            nfds_t j = 0; // j counts up on a valid entry

            for (nfds_t i = 0; i < n; i++) {
                struct pollfd* pfd = &fds[i];
                mxio_t* io = ios[i];

                if (io == NULL) {
                    // skip an invalid entry
                    continue;
                }
                if (j < nvalid) {
                    uint32_t events = 0;
                    io->ops->wait_end(io, items[j].pending, &events);
                    // mask unrequested events except HUP/ERR
                    pfd->revents = events & (pfd->events | POLLHUP | POLLERR);
                    if (pfd->revents != 0) {
                        nfds++;
                    }
                }
                j++;
            }
        }
    }

    for (int i = 0; i <= ios_used_max; i++) {
        if (ios[i]) {
            mxio_release(ios[i]);
        }
    }

    return (r == MX_OK || r == MX_ERR_TIMED_OUT) ? nfds : ERROR(r);
}

int select(int n, fd_set* restrict rfds, fd_set* restrict wfds, fd_set* restrict efds,
           struct timeval* restrict tv) {
    if (n > FD_SETSIZE || n < 1) {
        return ERRNO(EINVAL);
    }

    mxio_t* ios[n];
    int ios_used_max = -1;

    mx_status_t r = MX_OK;
    int nvalid = 0;

    mx_wait_item_t items[n];

    for (int fd = 0; fd < n; fd++) {
        ios[fd] = NULL;

        uint32_t events = 0;
        if (rfds && FD_ISSET(fd, rfds))
            events |= POLLIN;
        if (wfds && FD_ISSET(fd, wfds))
            events |= POLLOUT;
        if (efds && FD_ISSET(fd, efds))
            events |= POLLERR;
        if (events == 0) {
            continue;
        }

        mxio_t* io;
        if ((io = fd_to_io(fd)) == NULL) {
            r = MX_ERR_BAD_HANDLE;
            break;
        }
        ios[fd] = io;
        ios_used_max = fd;

        mx_handle_t h;
        mx_signals_t sigs;
        io->ops->wait_begin(io, events, &h, &sigs);
        if (h == MX_HANDLE_INVALID) {
            r = MX_ERR_INVALID_ARGS;
            break;
        }
        items[nvalid].handle = h;
        items[nvalid].waitfor = sigs;
        items[nvalid].pending = 0;
        nvalid++;
    }

    int nfds = 0;
    if (r == MX_OK && nvalid > 0) {
        mx_time_t tmo = (tv == NULL) ? MX_TIME_INFINITE :
            mx_deadline_after(MX_SEC(tv->tv_sec) + MX_USEC(tv->tv_usec));
        r = mx_object_wait_many(items, nvalid, tmo);
        // pending signals could be reported on MX_ERR_TIMED_OUT case as well
        if (r == MX_OK || r == MX_ERR_TIMED_OUT) {
            int j = 0; // j counts up on a valid entry

            for (int fd = 0; fd < n; fd++) {
                mxio_t* io = ios[fd];
                if (io == NULL) {
                    // skip an invalid entry
                    continue;
                }
                if (j < nvalid) {
                    uint32_t events = 0;
                    io->ops->wait_end(io, items[j].pending, &events);
                    if (rfds && FD_ISSET(fd, rfds)) {
                        if (events & POLLIN) {
                            nfds++;
                        } else {
                            FD_CLR(fd, rfds);
                        }
                    }
                    if (wfds && FD_ISSET(fd, wfds)) {
                        if (events & POLLOUT) {
                            nfds++;
                        } else {
                            FD_CLR(fd, wfds);
                        }
                    }
                    if (efds && FD_ISSET(fd, efds)) {
                        if (events & POLLERR) {
                            nfds++;
                        } else {
                            FD_CLR(fd, efds);
                        }
                    }
                } else {
                    if (rfds) {
                        FD_CLR(fd, rfds);
                    }
                    if (wfds) {
                        FD_CLR(fd, wfds);
                    }
                    if (efds) {
                        FD_CLR(fd, efds);
                    }
                }
                j++;
            }
        }
    }

    for (int i = 0; i <= ios_used_max; i++) {
        if (ios[i]) {
            mxio_release(ios[i]);
        }
    }

    return (r == MX_OK || r == MX_ERR_TIMED_OUT) ? nfds : ERROR(r);
}

int ioctl(int fd, int req, ...) {
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return ERRNO(EBADF);
    }
    va_list ap;
    va_start(ap, req);
    ssize_t r = io->ops->posix_ioctl(io, req, ap);
    va_end(ap);
    mxio_release(io);
    return STATUS(r);
}

ssize_t sendto(int fd, const void* buf, size_t buflen, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    ssize_t r = io->ops->sendto(io, buf, buflen, flags, addr, addrlen);
    mxio_release(io);
    return r < 0 ? STATUS(r) : r;
}

ssize_t recvfrom(int fd, void* restrict buf, size_t buflen, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    if (addr != NULL && addrlen == NULL) {
        return ERRNO(EFAULT);
    }
    ssize_t r = io->ops->recvfrom(io, buf, buflen, flags, addr, addrlen);
    mxio_release(io);
    return r < 0 ? STATUS(r) : r;
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    ssize_t r = io->ops->sendmsg(io, msg, flags);
    mxio_release(io);
    return r < 0 ? STATUS(r) : r;
}

ssize_t recvmsg(int fd, struct msghdr* msg, int flags) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    ssize_t r = io->ops->recvmsg(io, msg, flags);
    mxio_release(io);
    return r < 0 ? STATUS(r) : r;
}

int shutdown(int fd, int how) {
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return ERRNO(EBADF);
    }
    mx_status_t r = io->ops->shutdown(io, how);
    mxio_release(io);
    if (r == MX_ERR_BAD_STATE) {
        return ERRNO(ENOTCONN);
    }
    if (r == MX_ERR_WRONG_TYPE) {
        return ERRNO(ENOTSOCK);
    }
    return STATUS(r);
}
