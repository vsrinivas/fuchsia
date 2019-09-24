// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// !!! DEPRECATED !!!
// New usages should reference sdk/lib/sys/cpp/...

#ifndef SRC_LIB_COMPONENT_CPP_TESTING_TEST_UTIL_H_
#define SRC_LIB_COMPONENT_CPP_TESTING_TEST_UTIL_H_

#include <fuchsia/sys/cpp/fidl.h>

#include <fs/vfs.h>
#include <fs/vnode.h>

// TODO(anmittal): Move to some public library so that everyone can use these
// functions.

namespace component {
namespace testing {

zx::channel OpenAsDirectory(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> node);

}  // namespace testing
}  // namespace component

#endif  // SRC_LIB_COMPONENT_CPP_TESTING_TEST_UTIL_H_
