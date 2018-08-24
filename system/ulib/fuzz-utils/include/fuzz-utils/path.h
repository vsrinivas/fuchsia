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

    // Returns an absolute path to the file described by |relpath|.
    fbl::String Join(const char* relpath) const;

    // Resets the current path to point at the filesystem root.
    void Reset();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Path);

    // |fuzzing::Path::PathBuffer| defines a reference-counted string buffer that can be shared
    // between multiple |fuzzing::Path| objects chained together by |Push|.
    struct PathBuffer final : public fbl::RefCounted<PathBuffer> {
        fbl::StringBuffer<PATH_MAX> buffer_;
    };

    // The reference-counted string buffer shared by |push|-chained |Path| objects.
    fbl::RefPtr<PathBuffer> path_;
    // The amount of |buffer_| belonging to this |Path| object.  The buffer will be reset to this
    // length by |Pop|.
    size_t length_;
};

} // namespace fuzzing
