// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/wire_messaging_declarations.h>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "src/storage/f2fs/vnode.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

class AsyncTearDownVnode : public VnodeF2fs {
 public:
  AsyncTearDownVnode(F2fs* fs, ino_t ino, sync_completion_t* completions)
      : VnodeF2fs(fs, ino), callback_(nullptr), completions_(completions) {}

  ~AsyncTearDownVnode() {
    // C) Tear down the Vnode.
    sync_completion_signal(&completions_[2]);
  }

 private:
  void Sync(fs::Vnode::SyncCallback callback) final {
    callback_ = std::move(callback);
    std::thread thrd(&AsyncTearDownVnode::SyncThread, this);
    thrd.detach();
  }

  static void SyncThread(AsyncTearDownVnode* arg) {
    fs::Vnode::SyncCallback callback;
    {
      fbl::RefPtr<AsyncTearDownVnode> async_vn = fbl::RefPtr(arg);
      // A) Identify when the sync has started being processed.
      sync_completion_signal(&async_vn->completions_[0]);
      // B) Wait until the connection has been closed.
      sync_completion_wait(&async_vn->completions_[1], ZX_TIME_INFINITE);
      callback = std::move(async_vn->callback_);
    }
    callback(ZX_OK);
  }

  fs::Vnode::SyncCallback callback_;
  sync_completion_t* completions_;
};

// TODO(fxbug.dev/94157): Stop relying on FIDL internals like TransactionalRequest.header.
void SendDirSync(fidl::UnownedClientEnd<fuchsia_io::Directory> client) {
  FIDL_ALIGNDECL
  fidl::internal::TransactionalRequest<fuchsia_io::Directory::Sync> request;
  fidl::unstable::OwnedEncodedMessage<
      fidl::internal::TransactionalRequest<fuchsia_io::Directory::Sync>>
      encoded(&request);
  ASSERT_EQ(encoded.status(), ZX_OK);
  encoded.GetOutgoingMessage().set_txid(5);
  encoded.Write(zx::unowned_channel(client.handle()));
  ASSERT_EQ(encoded.status(), ZX_OK);
}

TEST(Teardown, ShutdownOnNoConnections) {
  std::unique_ptr<f2fs::Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDiscard), 1), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  sync_completion_t root_completions[3], child_completions[3];

  // Create root directory connection.
  nid_t root_nid;
  ASSERT_TRUE(fs->GetNodeManager().AllocNid(root_nid));
  auto root_dir = fbl::AdoptRef(new AsyncTearDownVnode(fs.get(), root_nid, root_completions));
  fs->GetNodeManager().AllocNidDone(root_nid);
  root_dir->SetMode(S_IFDIR);

  zx::status root_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(root_endpoints.status_value(), ZX_OK);
  auto [root_client, root_server] = std::move(*root_endpoints);
  ASSERT_EQ(fs->ServeDirectory(std::move(root_dir),
                               fidl::ServerEnd<fuchsia_io::Directory>(root_server.TakeChannel())),
            ZX_OK);

  // A) Wait for root directory sync to begin.
  SendDirSync(root_client);
  sync_completion_wait(&root_completions[0], ZX_TIME_INFINITE);

  // Create child vnode connection.
  nid_t child_nid;
  ASSERT_TRUE(fs->GetNodeManager().AllocNid(child_nid));
  auto child_dir = fbl::AdoptRef(new AsyncTearDownVnode(fs.get(), child_nid, child_completions));
  fs->GetNodeManager().AllocNidDone(child_nid);
  child_dir->SetMode(S_IFDIR);

  zx::status child_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(child_endpoints.status_value(), ZX_OK);
  auto [child_client, child_server] = std::move(*child_endpoints);
  auto validated_options = child_dir->ValidateOptions(fs::VnodeConnectionOptions());
  ASSERT_TRUE(validated_options.is_ok());
  ASSERT_EQ(child_dir->Open(validated_options.value(), nullptr), ZX_OK);
  ASSERT_EQ(fs->Serve(std::move(child_dir), child_server.TakeChannel(), validated_options.value()),
            ZX_OK);

  // A) Wait for child vnode sync to begin.
  SendDirSync(child_client);
  sync_completion_wait(&child_completions[0], ZX_TIME_INFINITE);

  // Terminate root directory connection.
  root_client.reset();

  // B) Let complete sync.
  sync_completion_signal(&root_completions[1]);

  // C) Tear down root directory.
  sync_completion_wait(&root_completions[2], ZX_TIME_INFINITE);

  // Sleep for a while until filesystem shutdown completes.
  zx::nanosleep(zx::deadline_after(zx::sec(1)));
  ASSERT_FALSE(fs->IsTerminating());

  // Terminate child vnode connection.
  child_client.reset();

  // B) Let complete sync.
  sync_completion_signal(&child_completions[1]);

  // C) Tear down child vnode.
  sync_completion_wait(&child_completions[2], ZX_TIME_INFINITE);

  // Sleep for a while until filesystem shutdown completes.
  zx::nanosleep(zx::deadline_after(zx::sec(1)));
  ASSERT_TRUE(fs->IsTerminating());
}

}  // namespace
}  // namespace f2fs
