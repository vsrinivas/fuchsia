// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "launcher_impl.h"

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/svc/outgoing.h>
#include <zircon/status.h>

int main(int argc, char** argv) {
    async::Loop loop;
    async_t* async = loop.async();
    svc::Outgoing outgoing(async);

    zx_status_t status = outgoing.ServeFromStartupInfo();
    if (status != ZX_OK) {
        fprintf(stderr, "process-launcher: error: Failed to serve outgoing directory: %d (%s).\n",
                status, zx_status_get_string(status));
        return 1;
    }

    fbl::WAVLTree<process::LauncherImpl*, fbl::unique_ptr<process::LauncherImpl>> launchers;

    outgoing.public_dir()->AddEntry(
        "fuchsia.process.Launcher",
        fbl::MakeRefCounted<fs::Service>([async, &launchers](zx::channel request) {
            auto launcher = fbl::make_unique<process::LauncherImpl>(fbl::move(request));

            zx_status_t status = launcher->Begin(async);
            if (status != ZX_OK) {
                fprintf(stderr, "process-launcher: error: Failed to serve request: %d (%s).\n",
                        status, zx_status_get_string(status));
                return status;
            }

            auto ptr = launcher.get();
            launchers.insert(fbl::move(launcher));

            ptr->set_error_handler([ptr, &launchers](zx_status_t status) {
                // If we encounter an error, we tear down the launcher.
                launchers.erase(ptr);
            });
            return ZX_OK;
        }));

    return loop.Run();
}
