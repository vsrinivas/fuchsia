// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fuzz-utils/path.h>
#include <lib/zircon-internal/debug.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <utility>

#define ZXDEBUG 0

namespace fuzzing {

// Public methods

Path::Path() {
    fbl::AllocChecker ac;
    path_ = AdoptRef(new (&ac) PathBuffer());
    ZX_ASSERT(ac.check());
    Reset();
}

Path::Path(fbl::RefPtr<PathBuffer> path) : path_(path), length_(path->buffer_.length()) {}

Path::Path(const Path& other) : path_(other.path_), length_(other.length_) {}

Path::~Path() {}

fbl::String Path::Join(const char* relpath) const {
    ZX_DEBUG_ASSERT(relpath);

    fbl::StringBuffer<PATH_MAX> abspath;
    abspath.Append(c_str(), length_ - 1);

    // Add each path segment
    const char* p = relpath;
    const char* sep;
    while (p && (sep = strchr(p, '/'))) {
        // Skip repeated slashes
        if (p != sep) {
            abspath.Append('/');
            abspath.Append(p, sep - p);
        }
        p = sep + 1;
    }
    if (*p) {
        abspath.Append('/');
        abspath.Append(p);
    }

    return std::move(abspath);
}

zx_status_t Path::GetSize(const char* relpath, size_t* out) const {
    fbl::String abspath = Join(relpath);
    struct stat buf;
    if (stat(abspath.c_str(), &buf) != 0) {
        xprintf("Failed to get status for '%s': %s\n", abspath.c_str(), strerror(errno));
        return ZX_ERR_IO;
    }
    if (!S_ISREG(buf.st_mode)) {
        xprintf("Not a regular file (%08x): %s\n", buf.st_mode, abspath.c_str());
        return ZX_ERR_NOT_FILE;
    }
    if (out) {
        *out = buf.st_size;
    }
    return ZX_OK;
}

fbl::unique_ptr<StringList> Path::List() const {
    fbl::AllocChecker ac;
    fbl::unique_ptr<StringList> list(new (&ac) StringList());
    ZX_ASSERT(ac.check());

    DIR* dir = opendir(c_str());
    if (!dir) {
        return list;
    }
    auto close_dir = fbl::MakeAutoCall([&dir]() { closedir(dir); });

    struct dirent* ent;
    while ((ent = readdir(dir))) {
        if (strcmp(".", ent->d_name) != 0) {
            list->push_back(ent->d_name);
        }
    }
    return list;
}

zx_status_t Path::Ensure(const char* relpath) {
    ZX_DEBUG_ASSERT(relpath);
    zx_status_t rc;

    // First check if already exists
    fbl::String abspath = Join(relpath);
    struct stat buf;
    if (stat(abspath.c_str(), &buf) == 0 && S_ISDIR(buf.st_mode)) {
        return ZX_OK;
    }

    // Now recursively create the parent directories
    const char* sep = strrchr(relpath, '/');
    if (sep) {
        fbl::String prefix(relpath, sep - relpath);
        if ((rc = Ensure(prefix)) != ZX_OK) {
            xprintf("Failed to ensure parent directory: %s\n", zx_status_get_string(rc));
            return rc;
        }
    }

    // Finally, create the last directory
    if (mkdir(abspath.c_str(), 0777) != 0) {
        xprintf("Failed to make directory '%s': %s.\n", abspath.c_str(), strerror(errno));
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t Path::Push(const char* relpath) {
    ZX_DEBUG_ASSERT(relpath);
    if (*relpath == '\0') {
        xprintf("Can't push empty path.\n");
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::String abspath = Join(relpath);
    struct stat buf;
    if (stat(abspath.c_str(), &buf) != 0) {
        xprintf("Failed to get status for '%s': %s\n", abspath.c_str(), strerror(errno));
        return ZX_ERR_IO;
    }
    if (!S_ISDIR(buf.st_mode)) {
        xprintf("Not a directory: %s\n", abspath.c_str());
        return ZX_ERR_NOT_DIR;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<Path> cloned(new (&ac) Path(path_));
    ZX_ASSERT(ac.check());

    cloned->parent_.swap(parent_);
    parent_.swap(cloned);
    path_->buffer_.Clear();
    path_->buffer_.Append(abspath);
    path_->buffer_.Append('/');
    length_ = path_->buffer_.length();

    return ZX_OK;
}

void Path::Pop() {
    if (!parent_) {
        return;
    }
    length_ = parent_->length_;
    path_->buffer_.Resize(length_);
    fbl::unique_ptr<Path> parent;
    parent.swap(parent_->parent_);
    parent_.swap(parent);
}

zx_status_t Path::Remove(const char* relpath) {
    ZX_DEBUG_ASSERT(relpath);
    zx_status_t rc;

    fbl::String abspath = Join(relpath);
    struct stat buf;
    if (stat(abspath.c_str(), &buf) != 0) {
        // Ignore missing files
        if (errno != ENOENT) {
            xprintf("Failed to get status for '%s': %s\n", abspath.c_str(), strerror(errno));
            return ZX_ERR_IO;
        }

    } else if (S_ISDIR(buf.st_mode)) {
        // Recursively remove directories
        if ((rc = Push(relpath)) != ZX_OK) {
            xprintf("Failed to push subdirectory: %s\n", zx_status_get_string(rc));
            return rc;
        }
        auto pop = fbl::MakeAutoCall([this]() { Pop(); });

        auto names = List();
        for (const char* name = names->first(); name; name = names->next()) {
            if ((rc = Remove(name)) != ZX_OK) {
                xprintf("Failed to remove subdirectory: %s\n", zx_status_get_string(rc));
                return rc;
            }
        }
        if (rmdir(c_str()) != 0) {
            xprintf("Failed to remove directory '%s': %s\n", c_str(), strerror(errno));
            return ZX_ERR_IO;
        }
        return ZX_OK;

    } else {
        // Remove file
        if (unlink(abspath.c_str()) != 0) {
            xprintf("Failed to unlink '%s': %s\n", abspath.c_str(), strerror(errno));
            return ZX_ERR_IO;
        }
    }

    return ZX_OK;
}

zx_status_t Path::Rename(const char* old_relpath, const char* new_relpath) {
    fbl::String old_abspath = Join(old_relpath);
    fbl::String new_abspath = Join(new_relpath);
    if (rename(old_abspath.c_str(), new_abspath.c_str()) != 0) {
        xprintf("Failed to rename '%s' to '%s': %s.\n", old_abspath.c_str(), new_abspath.c_str(),
                strerror(errno));
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

void Path::Reset() {
    parent_.reset();
    path_->buffer_.Clear();
    path_->buffer_.Append("/");
    length_ = path_->buffer_.length();
}

} // namespace fuzzing
