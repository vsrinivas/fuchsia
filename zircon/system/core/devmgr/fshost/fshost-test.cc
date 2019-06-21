// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#include <zxtest/zxtest.h>

#include "fs-manager.h"
#include "registry.h"
#include "vnode.h"

namespace {

// Test that when no filesystems have been added to the fshost vnode, it
// stays empty.
TEST(VnodeTestCase, NoFilesystems) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    auto dir = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
    auto fshost_vn =
        fbl::AdoptRef<devmgr::fshost::Vnode>(new devmgr::fshost::Vnode(loop.dispatcher(), dir));

    fbl::RefPtr<fs::Vnode> node;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, dir->Lookup(&node, "0"));
}

// Test that when filesystem has been added to the fshost vnode, it appears
// in the supplied remote tracking directory.
TEST(VnodeTestCase, AddFilesystem) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    auto dir = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
    auto fshost_vn =
        fbl::AdoptRef<devmgr::fshost::Vnode>(new devmgr::fshost::Vnode(loop.dispatcher(), dir));

    // Adds a new filesystem to the fshost service node.
    // This filesystem should appear as a new entry within |dir|.
    zx::channel server, client;
    ASSERT_OK(zx::channel::create(0u, &server, &client));

    zx_handle_t client_value = client.get();
    ASSERT_OK(fshost_vn->AddFilesystem(std::move(client)));
    fbl::RefPtr<fs::Vnode> node;
    ASSERT_OK(dir->Lookup(&node, "0"));
    EXPECT_EQ(node->GetRemote(), client_value);
}

// Test that the manager responds to external signals for unmounting.
TEST(FsManagerTestCase, WatchExit) {
    zx::event event, controller;
    ASSERT_OK(zx::event::create(0u, &event));
    ASSERT_OK(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &controller));

    fbl::unique_ptr<devmgr::FsManager> manager;
    zx_status_t status = devmgr::FsManager::Create(std::move(event), &manager);
    ASSERT_OK(status);
    manager->WatchExit();

    // The manager should not have exited yet: No one has asked for an unmount.
    zx_signals_t pending;
    auto deadline = zx::deadline_after(zx::msec(10));
    ASSERT_EQ(ZX_ERR_TIMED_OUT,
              controller.wait_one(FSHOST_SIGNAL_EXIT_DONE, deadline, &pending));

    // Once we "SIGNAL_EXIT", we expect an "EXIT_DONE" response.
    ASSERT_OK(controller.signal(0, FSHOST_SIGNAL_EXIT));
    deadline = zx::deadline_after(zx::sec(1));
    EXPECT_OK(controller.wait_one(FSHOST_SIGNAL_EXIT_DONE, deadline, &pending));
    EXPECT_TRUE(pending & FSHOST_SIGNAL_EXIT_DONE);
}

} // namespace
