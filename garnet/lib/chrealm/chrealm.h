// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CHREALM_CHREALM_H_
#define GARNET_BIN_CHREALM_CHREALM_H_

#include <string>
#include <vector>

#include <lib/fdio/spawn.h>
#include <zircon/status.h>
#include <zircon/types.h>

namespace chrealm {

// Like |RunBinaryInRealmAsync|, but waits for the process to terminate.
zx_status_t RunBinaryInRealm(const std::string& realm_path, const char** argv, int64_t* return_code,
                             std::string* error);

// Spawns a process running the binary with a namespace reflecting |realm_path|.
// Does not block for the command to finish.
//
// |realm_path| is an argument to the realm directory in /hub.
// |argv| is a null-terminated list of arguments for the job.
// |job| is the job to run the process under. If invalid, defaults to the
// realm's job.
//
// |flags| is the set of flags to pass to |fdio_spawn|. FDIO_CLONE_NAMESPACE
// is masked out.
//
// |additional_actions| is a set of actions to pass to |fdio_spawn|, in
// addition to the actions to build the namespace. Defaults to empty.
//
// |proc| is a handle to the process that was launched.
zx_status_t SpawnBinaryInRealmAsync(const std::string& realm_path, const char** argv,
                                    zx_handle_t job, int32_t flags,
                                    const std::vector<fdio_spawn_action_t>& additional_actions,
                                    zx_handle_t* proc, std::string* error);

}  // namespace chrealm

#endif  // GARNET_BIN_CHREALM_CHREALM_H_
