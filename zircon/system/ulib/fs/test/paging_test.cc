// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/zx/vmar.h>

#include <iostream>

#include <fbl/unique_fd.h>
#include <fs/internal/connection.h>
#include <fs/paged_vfs.h>
#include <fs/paged_vnode.h>
#include <fs/pseudo_dir.h>
#include <zxtest/zxtest.h>

namespace fs {

namespace {

class PagingTestFile : public PagedVnode {
 public:
  PagingTestFile(PagedVfs* vfs, std::vector<uint8_t> data) : PagedVnode(vfs), data_(data) {}

  ~PagingTestFile() override {}

  // PagedVnode implementation:
  void VmoRead(uint64_t offset, uint64_t length) override {
    zx::vmo transfer;
    if (zx::vmo::create(length, 0, &transfer) != ZX_OK) {
      ASSERT_TRUE(vfs()->ReportPagerError(*this, ZX_PAGER_OP_FAIL, offset, length, 0).is_ok());
      return;
    }

    transfer.write(&data_[offset], 0, std::min(data_.size() - offset, length));
    ASSERT_TRUE(vfs()->SupplyPages(*this, offset, length, transfer, 0).is_ok());
  }

  // Vnode implementation:
  VnodeProtocolSet GetProtocols() const override { return fs::VnodeProtocol::kFile; }
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights,
                                     fs::VnodeRepresentation* info) final {
    if (protocol == fs::VnodeProtocol::kFile) {
      *info = fs::VnodeRepresentation::File();
      return ZX_OK;
    }
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) override {
    if (auto result = EnsureCreateVmo(data_.size()); result.is_error()) {
      return result.error_value();
    }
    return vmo().create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, data_.size(), out_vmo);
  }

 private:
  std::vector<uint8_t> data_;
};

class PagingTestVfs : public PagedVfs {
 public:
  PagingTestVfs(int num_pager_threads) : PagedVfs(num_pager_threads) {}

 private:
  zx_status_t RegisterConnection(std::unique_ptr<fs::internal::Connection> connection,
                                 zx::channel server_end) final {
    connections_.push_back(std::move(connection));
    EXPECT_OK(connections_.back().StartDispatching(std::move(server_end)));
    return ZX_OK;
  }
  void UnregisterConnection(fs::internal::Connection* connection) final {
    connections_.erase(*connection);
  }
  void Shutdown(ShutdownCallback handler) override { FAIL("Should never be reached in this test"); }
  bool IsTerminating() const final { return false; }
  void CloseAllConnectionsForVnode(const fs::Vnode& node,
                                   CloseAllConnectionsForVnodeCallback callback) final {
    FAIL("Should never be reached in this test");
  }

 private:
  fbl::DoublyLinkedList<std::unique_ptr<fs::internal::Connection>> connections_;
};

// This file has many pages and end in a non-page-boundary.
const char kFile1Name[] = "file1";
constexpr size_t kFile1Size = 4096 * 17 + 87;

class PagingTest : public zxtest::Test {
 public:
  PagingTest()
      : main_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        vfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    // Generate contents for the canned file. This uses a repeating pattern of an odd number of
    // bytes so we don't get a page-aligned pattern.
    file1_contents_.resize(kFile1Size);
    constexpr uint8_t kMaxByte = 253;
    uint8_t cur = 4;
    for (size_t i = 0; i < kFile1Size; i++) {
      if (cur >= kMaxByte)
        cur = 0;
      file1_contents_[i] = cur;
      cur++;
    }
  }

  ~PagingTest() {
    if (vfs_thread_) {
      vfs_loop_.Quit();
      vfs_thread_->join();
    }
  }

  // Creates the VFS and returns an FD to the root directory.
  int CreateVfs(int num_pager_threads) {
    // Start the VFS worker thread.
    vfs_thread_ = std::make_unique<std::thread>([this]() { VfsThreadProc(); });

    // Start the VFS and pager objects.
    vfs_ = std::make_unique<PagingTestVfs>(num_pager_threads);
    EXPECT_TRUE(vfs_->Init().is_ok());
    vfs_->SetDispatcher(vfs_loop_.dispatcher());

    // Set up the directory hierarchy.
    root_ = fbl::MakeRefCounted<PseudoDir>();
    file1_ = fbl::MakeRefCounted<PagingTestFile>(vfs_.get(), file1_contents_);
    root_->AddEntry(kFile1Name, file1_);

    // Connect to the root.
    zx::channel client_end, server_end;
    EXPECT_OK(zx::channel::create(0u, &client_end, &server_end));
    vfs_->ServeDirectory(root_, std::move(server_end));

    // Convert to an FD.
    int root_dir_fd = -1;
    EXPECT_OK(fdio_fd_create(client_end.release(), &root_dir_fd));

    return root_dir_fd;
  }

 protected:
  std::vector<uint8_t> file1_contents_;

 private:
  // The VFS needs to run on a separate thread to handle the FIDL requests from the test because
  // the FDIO calls from the main thread are blocking.
  void VfsThreadProc() { vfs_loop_.Run(); }

  // This test requires two threads because the main thread needs to make blocking calls that are
  // serviced by the VFS on the background thread.
  async::Loop main_loop_;
  std::unique_ptr<std::thread> vfs_thread_;
  async::Loop vfs_loop_;

  std::unique_ptr<PagingTestVfs> vfs_;

  fbl::RefPtr<PseudoDir> root_;
  fbl::RefPtr<PagingTestFile> file1_;
};

template <typename T>
T RoundUp(T value, T multiple) {
  return (value + multiple - 1) / multiple * multiple;
}

}  // namespace

TEST_F(PagingTest, Read) {
  fbl::unique_fd root_dir_fd(CreateVfs(1));
  ASSERT_TRUE(root_dir_fd);

  fbl::unique_fd file1_fd(openat(root_dir_fd.get(), kFile1Name, 0, S_IRWXU));
  ASSERT_TRUE(file1_fd);

  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, fdio_get_vmo_exact(file1_fd.get(), vmo.reset_and_get_address()));

  // Map the data and validate the result can be read.
  zx_vaddr_t mapped_addr = 0;
  ASSERT_EQ(ZX_OK,
            zx::vmar::root_self()->map(ZX_VM_PERM_READ, 0, vmo, 0,
                                       RoundUp<uint64_t>(kFile1Size, PAGE_SIZE), &mapped_addr));
  ASSERT_TRUE(mapped_addr);

  const uint8_t* mapped = reinterpret_cast<const uint8_t*>(mapped_addr);
  for (size_t i = 0; i < kFile1Size; i++) {
    ASSERT_EQ(mapped[i], file1_contents_[i]);
  }
}

// TODO(bug 51111):
//  - Test closing a file frees the PagedVnode object.
//  - Test pager error propagation.
//  - Test with vmo_read.
//  - Test multiple threads (deliberately hang one to make sure we can service another request).

}  // namespace fs
