// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/zx/vmar.h>

#include <iostream>
#include <optional>

#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/unique_fd.h>
#include <fs/internal/connection.h>
#include <fs/paged_vfs.h>
#include <fs/paged_vnode.h>
#include <fs/pseudo_dir.h>
#include <zxtest/zxtest.h>

namespace fs {

namespace {

// This structure tracks the mapped state of the paging test file across the test thread and the
// paging thread.
class SharedFileState {
 public:
  // Called by the PagedVnode when the VMO is mapped or unmapped.
  void SignalVmoPresenceChanged(bool present) {
    {
      fbl::AutoLock lock(&mutex_);

      vmo_present_changed_ = true;
      vmo_present_ = present;
    }

    cond_var_.Signal();
  }

  // Returns the current state of the mapped flag.
  bool GetVmoPresent() {
    fbl::AutoLock lock(&mutex_);
    return vmo_present_;
  }

  // Waits for the vmo presence to be marked changed and returns the presence flag.
  //
  // Called by the test to get the [un]mapped event.
  bool WaitForChangedVmoPresence() {
    fbl::AutoLock lock(&mutex_);

    while (!vmo_present_changed_)
      cond_var_.Wait(&mutex_);

    vmo_present_changed_ = false;
    return vmo_present_;
  }

 private:
  fbl::Mutex mutex_;
  fbl::ConditionVariable cond_var_;

  bool vmo_present_changed_ = false;
  bool vmo_present_ = false;
};

class PagingTestFile : public PagedVnode {
 public:
  PagingTestFile(PagedVfs* vfs, std::shared_ptr<SharedFileState> shared, std::vector<uint8_t> data)
      : PagedVnode(vfs), shared_(std::move(shared)), data_(data) {}

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
    // We need to signal after the VMO was mapped that it changed.
    bool becoming_mapped = !vmo();

    if (auto result = EnsureCreateVmo(data_.size()); result.is_error())
      return result.error_value();

    if (zx_status_t status =
            vmo().create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, data_.size(), out_vmo);
        status != ZX_OK)
      return status;

    if (becoming_mapped)
      shared_->SignalVmoPresenceChanged(true);
    return ZX_OK;
  }

 protected:
  void OnNoClones() override { shared_->SignalVmoPresenceChanged(false); }

 private:
  std::shared_ptr<SharedFileState> shared_;
  std::vector<uint8_t> data_;
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
    vfs_ = std::make_unique<PagedVfs>(vfs_loop_.dispatcher(), num_pager_threads);
    EXPECT_TRUE(vfs_->Init().is_ok());

    // Set up the directory hierarchy.
    root_ = fbl::MakeRefCounted<PseudoDir>();

    file1_shared_ = std::make_shared<SharedFileState>();
    file1_ = fbl::MakeRefCounted<PagingTestFile>(vfs_.get(), file1_shared_, file1_contents_);
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
  std::shared_ptr<SharedFileState> file1_shared_;
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

  std::unique_ptr<PagedVfs> vfs_;

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

  // With no VMO requests, there should be no mappings of the VMO in the file.
  ASSERT_FALSE(file1_shared_->GetVmoPresent());

  // Gets the VMO for file1, it should now have a VMO.
  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, fdio_get_vmo_exact(file1_fd.get(), vmo.reset_and_get_address()));
  ASSERT_TRUE(file1_shared_->WaitForChangedVmoPresence());

  // Map the data and validate the result can be read.
  zx_vaddr_t mapped_addr = 0;
  size_t mapped_len = RoundUp<uint64_t>(kFile1Size, PAGE_SIZE);
  ASSERT_EQ(ZX_OK,
            zx::vmar::root_self()->map(ZX_VM_PERM_READ, 0, vmo, 0, mapped_len, &mapped_addr));
  ASSERT_TRUE(mapped_addr);

  // Clear the VMO so the code below also validates that the mapped memory works even when the
  // VMO is freed. The mapping stores an implicit reference to the vmo.
  vmo.reset();

  const uint8_t* mapped = reinterpret_cast<const uint8_t*>(mapped_addr);
  for (size_t i = 0; i < kFile1Size; i++) {
    ASSERT_EQ(mapped[i], file1_contents_[i]);
  }

  // The vmo should still be valid.
  ASSERT_TRUE(file1_shared_->GetVmoPresent());

  // Unmap the memory. This should notify the vnode which should free its vmo_ reference.
  ASSERT_EQ(ZX_OK, zx::vmar::root_self()->unmap(mapped_addr, mapped_len));
  ASSERT_FALSE(file1_shared_->WaitForChangedVmoPresence());
}

// TODO(bug 51111):
//  - Test closing a file frees the PagedVnode object.
//  - Test pager error propagation.
//  - Test with vmo_read.
//  - Test multiple threads (deliberately hang one to make sure we can service another request).

}  // namespace fs
