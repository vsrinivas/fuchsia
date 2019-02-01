// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_TESTING_TEST_UTIL_H_
#define LIB_COMPONENT_CPP_TESTING_TEST_UTIL_H_

#include <fs/vfs.h>
#include <fs/vnode.h>
#include <fuchsia/sys/cpp/fidl.h>

// TODO(anmittal): Move to some public library so that everyone can use these
// functions.

namespace component {
namespace testing {

fuchsia::sys::FileDescriptorPtr CloneFileDescriptor(int fd);

zx::channel OpenAsDirectory(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> node);

}  // namespace testing
}  // namespace component

#endif  // LIB_COMPONENT_CPP_TESTING_TEST_UTIL_H_
