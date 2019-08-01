// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/scoped_temp_file.h"

#include <stdlib.h>
#include <unistd.h>

namespace zxdb {

ScopedTempFile::ScopedTempFile()
    : name_("/tmp/zxdb_temp.XXXXXX"), fd_(mkstemp(const_cast<char*>(name_.c_str()))) {}

ScopedTempFile::~ScopedTempFile() {
  fd_.reset();
  remove(name_.c_str());
}

}  // namespace zxdb
