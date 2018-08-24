// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fuzz-utils/path.h>
#include <lib/fdio/debug.h>
#include <zircon/status.h>

#define ZXDEBUG 0

namespace fuzzing {

// Public methods

Path::Path() {
    fbl::AllocChecker ac;
    path_ = AdoptRef(new (&ac) PathBuffer());
    ZX_ASSERT(ac.check());
    Reset();
}

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

    return fbl::move(abspath);
}

void Path::Reset() {
    path_->buffer_.Clear();
    path_->buffer_.Append("/");
    length_ = path_->buffer_.length();
}

} // namespace fuzzing
