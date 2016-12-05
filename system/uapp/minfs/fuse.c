#define FUSE_USE_VERSION 26
#define _XOPEN_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <fuse/fuse.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fs/trace.h>

#include "minfs-private.h"

#ifdef DEBUG
#define debug(fmt...) fprintf(stderr, fmt)
#else
#define debug(fmt...)
#endif

// TODO(jpoichet) Simplify to use vfs_walk, which means get rid of fake_root
// and call mount before every call. Then vfs_close can easily be used without
// checking, as well as vfs_rename for the rename callback.

static vnode_t* fake_root;
static bcache_t* the_block_cache;
static pthread_mutex_t bc_lock;

#define LOCK() pthread_mutex_lock(&bc_lock);
#define UNLOCK() pthread_mutex_unlock(&bc_lock);

static int vnode_for_path(const char* path, vnode_t** out) {
    vnode_t *vn, *cur = fake_root;
    mx_status_t status;
    const char* nextpath = NULL;
    size_t len;

    do {
        while (path[0] == '/') {
            path++;
        }
        if (path[0] == 0) {
            path = ".";
        }
        len = strlen(path);
        nextpath = strchr(path, '/');
        if (nextpath != NULL) {
            len = nextpath - path;
            nextpath++;
            debug("fuse-minfs: nextpath = %s\n", nextpath);
        }
        status = cur->ops->lookup(cur, &vn, path, len);
        if (status != NO_ERROR) {
            debug("fuse-minfs: file %s not found: %d\n", path, status);
            return -ENOENT;
        }
        if (cur != fake_root) {
            vfs_close(cur);
        }
        cur = vn;
        path = nextpath;
    } while (nextpath != NULL);

    *out = vn;
    return 0;
}

static int getattr_callback(const char* path, struct stat* stbuf) {
    debug("fuse-minfs: [gattr] %s\n", path);
    vnode_t* vn;
    LOCK();
    int r = vnode_for_path(path, &vn);
    if (r) {
        UNLOCK();
        return r;
    }
    vnattr_t attr;
    mx_status_t status = vn->ops->getattr(vn, &attr);
    if (vn != fake_root) {
        vfs_close(vn);
    }
    UNLOCK();
    if (status != NO_ERROR) {
        debug("fuse-minfs: failed to retrieve attributes for %s: %d\n", path, status);
        return -EIO;
    }
    memset(stbuf, 0, sizeof(struct stat));
    debug("fuse-minfs: getattr file %s mode: %d size: %lu inode: %lu\n", path, attr.mode, attr.size, attr.inode);
    stbuf->st_mode = attr.mode;
    stbuf->st_size = attr.size;
    stbuf->st_ino = attr.inode;
    stbuf->st_ctime = attr.create_time / 1000000;
    stbuf->st_mtime = attr.modify_time / 1000000;
    return 0;
}

static int readdir_callback(const char* path, void* buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info* fi) {
    debug("fuse-minfs: [readdir] '%s'\n", path);
    vdircookie_t dircookie;

    const char* pathout;
    vnode_t* vn;
    LOCK();
    mx_status_t status = vfs_open(fake_root, &vn, path, &pathout, fi->flags, 0);
    if (status != NO_ERROR) {
        debug("fuse-minfs: failed to open %s: %d\n", path, status);
        UNLOCK();
        return -EIO;
    }

    uint8_t dirents[2048];
    size_t len = sizeof(dirents);
    status = vn->ops->readdir(vn, &dircookie, &dirents, len);
    if (vn != fake_root) {
        vfs_close(vn);
    }
    if (status < 0) {
        debug("fuse-minfs: failed to readdir %s: %d\n", path, status);
        UNLOCK();
        return -EIO;
    }

    debug("fuse-minfs: readdir %s: %d\n", path, status);
    size_t size = status;
    uint8_t* ptr = &dirents[0];
    while (size >= sizeof(vdirent_t)) {
        vdirent_t* vde = (void*)ptr;
        if (vde->size == 0)
            break;
        if (size < vde->size)
            break;

        debug("fuse-minfs: size %u/%zu type %u name %s\n", vde->size, size, vde->type, vde->name);
        if (filler(buf, vde->name, NULL, 0) != 0) {
            UNLOCK();
            return -ENOMEM;
        }
        ptr += vde->size;
        size -= vde->size;
    }
    UNLOCK();
    return 0;
}

static int open_callback(const char* path, struct fuse_file_info* fi) {
    debug("fuse-minfs: [open] %s\n", path);
    const char* pathout;
    vnode_t* vn;
    // flags can't contain O_CREAT so mode can be 0
    LOCK();
    mx_status_t status = vfs_open(fake_root, &vn, path, &pathout, fi->flags, 0);
    if (status < 0) {
        debug("fuse-minfs: failed to open %s: %d\n", path, status);
        UNLOCK();
        return -EIO;
    }
    // Only really checking file existence on open
    if (vn != fake_root) {
        vfs_close(vn);
    }
    UNLOCK();
    return 0;
}

static int read_callback(const char* path, char* buf, size_t size, off_t offset,
                         struct fuse_file_info* fi) {
    debug("fuse-minfs: [read] %s %zu %ld\n", path, size, offset);
    const char* pathout;
    vnode_t* vn;
    LOCK();
    mx_status_t status = vfs_open(fake_root, &vn, path, &pathout, fi->flags, 0);
    if (status < 0) {
        debug("fuse-minfs: failed to open %s: %d\n", path, status);
        UNLOCK();
        return -EIO;
    }

    ssize_t r = vn->ops->read(vn, buf, size, offset);
    if (vn != fake_root) {
        vfs_close(vn);
    }
    UNLOCK();
    return r;
}

static int mknod_callback(const char* path, mode_t mode, dev_t dev) {
    debug("fuse-minfs: [mknod] %s %x\n", path, mode);
    const char* pathout;
    vnode_t* vn;
    LOCK();
    mx_status_t status = vfs_open(fake_root, &vn, path, &pathout, O_CREAT | O_EXCL, mode);
    if (status < 0) {
        debug("fuse-minfs: failed to create directory %s: %d\n", path, status);
        UNLOCK();
        return -EIO;
    }
    debug("fuse-minfs: created node %s\n", path);
    if (vn != fake_root) {
        vfs_close(vn);
    }
    UNLOCK();
    return 0;
}

static int mkdir_callback(const char* path, mode_t mode) {
    debug("fuse-minfs: [mkdir] %s %x\n", path, mode);
    mode &= 0777;
    mode |= S_IFDIR;
    return mknod_callback(path, mode, 0);
}

static int write_callback(const char* path, const char* buffer, size_t size, off_t offset,
                          struct fuse_file_info* fi) {
    debug("fuse-minfs: [write] %s %zu %ld\n", path, size, offset);
    const char* pathout;
    vnode_t* vn;
    LOCK();
    mx_status_t status = vfs_open(fake_root, &vn, path, &pathout, fi->flags, 0);
    if (status < 0) {
        debug("fuse-minfs: failed to open %s: %d\n", path, status);
        UNLOCK();
        return -EIO;
    }

    ssize_t r = vn->ops->write(vn, buffer, size, offset);
    if (vn != fake_root) {
        vfs_close(vn);
    }
    UNLOCK();
    return r;
}

static int unlink_callback(const char* path) {
    debug("fuse-minfs: [unlink] %s\n", path);
    const char* pathout;
    vnode_t* vn;
    LOCK();
    mx_status_t status = vfs_walk(fake_root, &vn, path, &pathout);
    if (status == NO_ERROR) {
        debug("fuse-minfs: found %s\n", pathout);
        status = vn->ops->unlink(vn, pathout, strlen(pathout));
        debug("fuse-minfs: unlink = %d\n", status);
        if (vn != fake_root) {
            vfs_close(vn);
        }
    }
    UNLOCK();
    return status;
}

static int rmdir_callback(const char* path) {
    debug("fuse-minfs: [rmdir] %s\n", path);
    return unlink_callback(path);
}

static int rename_callback(const char* oldpath, const char* newpath) {
    debug("fuse-minfs: [rename] %s to %s\n", oldpath, newpath);
    vnode_t *oldparent, *newparent;
    mx_status_t r = 0, r_old, r_new;
    LOCK();
    r_old = vfs_walk(fake_root, &oldparent, oldpath, &oldpath);
    if (r_old < 0) {
        UNLOCK();
        debug("fuse-minfs: could not find %s\n", oldpath);
        return -ENOENT;
    }
    r_new = vfs_walk(fake_root, &newparent, newpath, &newpath);
    if (r_new < 0) {
        UNLOCK();
        debug("fuse-minfs: could not find %s\n", newpath);
        return -ENOENT;
    }
    // Make sure it's in the same filesystem
    if (r_new != r_old) {
        UNLOCK();
        debug("fuse-minfs: old and new not in same filesystem %d %d\n", r_new, r_old);
        return -ENOENT;
    }
    r = fake_root->ops->rename(oldparent, newparent, oldpath, strlen(oldpath), newpath, strlen(newpath));
    if (oldparent != fake_root) {
        vfs_close(oldparent);
    }
    if (newparent != fake_root) {
        vfs_close(newparent);
    }
    debug("fuse-minfs: rename result: %d\n", r);
    UNLOCK();
    return r == NO_ERROR ? 0 : -EIO;
}

static int truncate_callback(const char* path, off_t offset) {
    debug("fuse-minfs: [truncate] %s %ld\n", path, offset);
    vnode_t* vn;
    LOCK();
    int r = vnode_for_path(path, &vn);
    if (r) {
        UNLOCK();
        debug("fuse-minfs: could not find node for %s: %d\n", path, r);
        return r;
    }
    mx_status_t status = vn->ops->truncate(vn, offset);
    if (vn != fake_root) {
        vfs_close(vn);
    }
    UNLOCK();
    if (status != NO_ERROR) {
        debug("fuse-minfs: could not truncate %s: %d\n", path, status);
        return -EIO;
    }
    return 0;
}

static int utimens_callback(const char* path, const struct timespec tv[2]) {
    debug("fuse-minfs: [utimens] %s %ld %ld\n", path, tv[0].tv_sec * 1000000 + tv[0].tv_nsec / 1000, tv[1].tv_sec * 1000000 + tv[1].tv_nsec / 1000);
    vnode_t* vn;
    LOCK();
    int r = vnode_for_path(path, &vn);
    if (r) {
        UNLOCK();
        debug("fuse-minfs: could not find node for %s: %d\n", path, r);
        return r;
    }
    vnattr_t attr;
    mx_status_t status = vn->ops->getattr(vn, &attr);
    if (status != NO_ERROR) {
        UNLOCK();
        debug("fuse-minfs: failed to retrieve attributes for %s: %d\n", path, status);
        return -EIO;
    }
    debug("fuse-minfs: attr %lu -> %ld\n", attr.modify_time, tv[1].tv_sec * 1000000 + tv[1].tv_nsec / 1000);
    attr.valid = ATTR_CTIME | ATTR_MTIME;
    attr.create_time = tv[0].tv_sec * 1000000 + tv[0].tv_nsec / 1000;
    attr.modify_time = tv[1].tv_sec * 1000000 + tv[1].tv_nsec / 1000;
    status = vn->ops->setattr(vn, &attr);
    if (vn != fake_root) {
        vfs_close(vn);
    }
    UNLOCK();
    if (status != NO_ERROR) {
        debug("fuse-minfs: failed to set attributes for %s: %d\n", path, status);
        return -EIO;
    }
    return 0;
}

static int chown_callback(const char* path, uid_t uid, gid_t gid) {
    debug("fuse-minfs: [chown] %s (%d:%d)\n", path, uid, gid);
    // Ignore, but required for "setattr" to be supported
    return 0;
}

static int chmod_callback(const char *path, mode_t mode) {
    debug("fuse-minfs: [chmod] %s (%d)\n", path, mode);
  return 0;
}

static int access_callback(const char *path, int mode) {
  debug("fuse-minfs: [access] %s %d\n", path, mode);
  if (mode & F_OK) {
    // make sure file exists
    LOCK();
    vnode_t *vn;
    int r = vnode_for_path(path, &vn);
    if (r) {
      UNLOCK();
      debug("fuse-minfs: file not found %s\n", path);
      return -1;
    }
    if (vn != fake_root) {
      vfs_close(vn);
    }
  }
  return 0;
}

static struct fuse_operations fuse_minfs_operations = {
    .getattr = getattr_callback,
    .open = open_callback,
    .read = read_callback,
    .readdir = readdir_callback,
    .mknod = mknod_callback,
    .mkdir = mkdir_callback,
    .write = write_callback,
    .rmdir = rmdir_callback,
    .unlink = unlink_callback,
    .rename = rename_callback,
    .truncate = truncate_callback,
    .utimens = utimens_callback,
    .chown = chown_callback,
    .chmod = chmod_callback,
    .access = access_callback,
};

off_t get_size(int fd, off_t* out) {
    struct stat s;
    if (fstat(fd, &s) < 0) {
        fprintf(stderr, "fuse-minfs: could not find end of file/device\n");
        return -1;
    }
    *out = s.st_size;
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        return fuse_main(argc, argv, &fuse_minfs_operations, NULL);
    } else {
#ifdef DEBUG
        trace_on(TRACE_MINFS | TRACE_VFS | TRACE_WALK);
#endif
        const char* block = argv[1];
        int fd;
        uint32_t flags = O_RDWR;
        off_t size = 0;
        bcache_t* bc;
        vnode_t* vn = 0;

        info("Mounting %s\n", block);
        if ((fd = open(block, flags, 0644)) < 0) {
            error("fuse-minfs: cannot open '%s'\n", block);
            return -1;
        }
        if (get_size(fd, &size)) {
          error("fuse-minfs: could not determine size of %s\n", block);
          return -1;
        }
        size /= MINFS_BLOCK_SIZE;

        if (bcache_create(&bc, fd, size, MINFS_BLOCK_SIZE, 64) < 0) {
            error("fuse-minfs: cannot create block cache\n");
            return -1;
        }

        if (minfs_mount(&vn, bc) < 0) {
            error("fuse-minfs: could not mount filesystem\n");
            return -1;
        }
        fake_root = vn;
        the_block_cache = bc;

        return fuse_main(argc - 1, argv + 1, &fuse_minfs_operations, NULL);
    }
}
