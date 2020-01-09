// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CMD_LEGACY_BUILTINS_H_
#define SRC_DEVELOPER_CMD_LEGACY_BUILTINS_H_

#include <zircon/compiler.h>

__BEGIN_CDECLS

// There isn't a header in //zircon/third_party/uapp/dash that contains the declarations. Instead,
// dash just lists the prototypes like we do here.
int zxc_ls(int, const char**);
int zxc_mv_or_cp(int, const char**);
int zxc_mkdir(int, const char**);
int zxc_rm(int, const char**);
int zxc_dump(int, const char**);
int zxc_list(int, const char**);
int zxc_msleep(int, const char**);
int zxc_dm(int, const char**);
int zxc_k(int, const char**);

__END_CDECLS

#endif  // SRC_DEVELOPER_CMD_LEGACY_BUILTINS_H_
