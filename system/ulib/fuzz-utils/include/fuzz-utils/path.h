// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <stddef.h>

#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_ptr.h>
#include <fuzz-utils/string-list.h>
#include <zircon/types.h>

namespace fuzzing {

// |fuzzing::Path| is a utility class for interacting with files on the filesystem. In the methods
// below, |relpath| must not be null and is a relative path under the current path.
class Path final {
public:
    Path();
    ~Path();

    // Returns the current path as a C-style string.
    const char* c_str() const { return path_->buffer_.c_str(); }
    size_t length() const { return path_->buffer_.length(); }

    // Returns an absolute path to the file described by |relpath|.
    fbl::String Join(const char* relpath) const;
    fbl::String Join(const fbl::String& relpath) const { return Join(relpath.c_str()); }

    // Returns whether the given |relpath| is present and is a regular file.
    bool IsFile(const char *relpath) const { return GetSize(relpath, nullptr) == ZX_OK; }
    bool IsFile(const fbl::String &relpath) const { return IsFile(relpath.c_str()); }

    // Returns the size of the file in |out|, if it exists. |out| is unchanged on error.
    zx_status_t GetSize(const char* relpath, size_t* out) const;
    zx_status_t GetSize(const fbl::String& relpath, size_t* out) const {
        return GetSize(relpath.c_str(), out);
    }

    // Returns a list of files in the directory given by the current path.
    fbl::unique_ptr<StringList> List() const;

    // Checks if a directory exists at |relpath| and creates on if it does not.
    zx_status_t Ensure(const char* relpath);
    zx_status_t Ensure(const fbl::String& relpath) { return Ensure(relpath.c_str()); }

    // Changes the current path to the directory described by |relpath|.
    zx_status_t Push(const char* relpath);
    zx_status_t Push(const fbl::String& relpath) { return Push(relpath.c_str()); }

    // Changes to current path to value before the corresponding |Push|.  Does nothing if already at
    // the filesystem root.
    void Pop();

    // Deletes the file described by |relpath|, if it exists.
    zx_status_t Remove(const char* relpath);
    zx_status_t Remove(const fbl::String& relpath) { return Remove(relpath.c_str()); }

    // Moves and/or renames the file described by |old_relpath| to |new_relpath|.
    zx_status_t Rename(const char* old_relpath, const char* new_relpath);
    zx_status_t Rename(const fbl::String& old_relpath, const fbl::String& new_relpath) {
        return Rename(old_relpath.c_str(), new_relpath.c_str());
    }

    // Resets the current path to point at the filesystem root.
    void Reset();

private:
    // |fuzzing::Path::PathBuffer| defines a reference-counted string buffer that can be shared
    // between multiple |fuzzing::Path| objects chained together by |Push|.
    struct PathBuffer final : public fbl::RefCounted<PathBuffer> {
        fbl::StringBuffer<PATH_MAX> buffer_;
    };

    explicit Path(fbl::RefPtr<PathBuffer> path);
    explicit Path(const Path& other);

    Path(Path&&) = delete;
    Path& operator=(const Path&) = delete;
    Path& operator=(Path&&) = delete;

    // The preceding |Path| object as set by |Push|.
    fbl::unique_ptr<Path> parent_;
    // The reference-counted string buffer shared by |push|-chained |Path| objects.
    fbl::RefPtr<PathBuffer> path_;
    // The amount of |buffer_| belonging to this |Path| object.  The buffer will be reset to this
    // length by |Pop|.
    size_t length_;
};

} // namespace fuzzing
