// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>

#include "vnode.h"

namespace fs {

// A remote directory holds a channel to a remotely hosted directory to
// which requests are delegated when opened.
//
// This class is designed to allow programs to publish remote filesystems
// as directories without requiring a separate "mount" step.  In effect,
// a remote directory is "mounted" at creation time.
//
// It is not possible for the client to detach the remote directory or
// to mount a new one in its place.
//
// This class is thread-safe.
class RemoteDir : public Vnode {
public:
    // Binds to a remotely hosted directory using the specified RIO client
    // channel endpoint.  The channel must be valid.
    RemoteDir(zx::channel remote_dir_client);

    // Releases the remotely hosted directory.
    ~RemoteDir() override;

    // |Vnode| implementation:
    zx_status_t Getattr(vnattr_t* a) final;
    bool IsRemote() const final;
    zx_handle_t GetRemote() const final;

private:
    zx::channel const remote_dir_client_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(RemoteDir);
};

} // namespace fs
