// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/internal.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmar.h>
#include <zircon/syscalls-next.h>

#include <algorithm>
#include <condition_variable>
#include <optional>
#include <utility>

#include <fbl/condition_variable.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/connection.h"
#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/lib/storage/vfs/cpp/paged_vnode.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace fs {

namespace {

// This structure tracks the mapped state of the paging test file across the test thread and the
// paging thread.
class SharedFileState {
 public:
  // Called by the PagedVnode when the VMO is mapped or unmapped.
  void SignalVmoPresenceChanged(bool present) {
    {
      std::lock_guard lock(mutex_);

      vmo_present_changed_ = true;
      vmo_present_ = present;
    }

    cond_var_.notify_one();
  }

  // Returns the current state of the mapped flag.
  bool GetVmoPresent() {
    std::lock_guard lock(mutex_);
    return vmo_present_;
  }

  // Waits for the vmo presence to be marked changed and returns the presence flag.
  //
  // Called by the test to get the [un]mapped event.
  bool WaitForChangedVmoPresence() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (!vmo_present_changed_)
      cond_var_.wait(lock);

    vmo_present_changed_ = false;
    return vmo_present_;
  }

 private:
  std::mutex mutex_;
  std::condition_variable cond_var_;

  bool vmo_present_changed_ = false;
  bool vmo_present_ = false;
};

class PagingTestFile : public PagedVnode {
 public:
  // Controls the success or failure that VmoRead() will report. Defaults to success (ZX_OK).
  void set_read_status(zx_status_t status) { vmo_read_status_ = status; }

  // Controls the success or failure that VmoDirty() will report. Defaults to success (ZX_OK).
  void set_dirty_status(zx_status_t status) { vmo_dirty_status_ = status; }

  // Public locked version of PagedVnode::has_clones().
  bool HasClones() const {
    std::lock_guard lock(mutex_);
    return has_clones();
  }

  // PagedVnode implementation:
  void VmoRead(uint64_t offset, uint64_t length) override {
    std::lock_guard lock(mutex_);

    if (vmo_read_status_ != ZX_OK) {
      // We're supposed to report errors.
      std::optional vfs = this->vfs();
      ASSERT_TRUE(vfs.has_value());
      const zx::result result =
          vfs.value().get().ReportPagerError(paged_vmo(), offset, length, ZX_ERR_IO_DATA_INTEGRITY);
      ASSERT_OK(result.status_value());
      return;
    }

    zx::vmo transfer;
    if (zx::vmo::create(length, 0, &transfer) != ZX_OK) {
      std::optional vfs = this->vfs();
      ASSERT_TRUE(vfs.has_value());
      const zx::result result =
          vfs.value().get().ReportPagerError(paged_vmo(), offset, length, ZX_ERR_BAD_STATE);
      ASSERT_OK(result.status_value());
    }

    transfer.write(&data_[offset], 0, std::min(data_.size() - offset, length));
    std::optional vfs = this->vfs();
    ASSERT_TRUE(vfs.has_value());
    const zx::result result =
        vfs.value().get().SupplyPages(paged_vmo(), offset, length, transfer, 0);
    ASSERT_OK(result.status_value());
  }

  void VmoDirty(uint64_t offset, uint64_t length) override {
    std::lock_guard lock(mutex_);

    if (vmo_dirty_status_ != ZX_OK) {
      // We're supposed to report errors.
      std::optional vfs = this->vfs();
      ASSERT_TRUE(vfs.has_value());
      const zx::result result =
          vfs.value().get().ReportPagerError(paged_vmo(), offset, length, ZX_ERR_IO_DATA_INTEGRITY);
      ASSERT_OK(result.status_value());
      return;
    }

    std::optional vfs = this->vfs();
    ASSERT_TRUE(vfs.has_value());
    const zx::result result = vfs.value().get().DirtyPages(paged_vmo(), offset, length);
    ASSERT_OK(result.status_value());
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
  zx_status_t GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo) override {
    std::lock_guard lock(mutex_);

    // We need to signal after the VMO was mapped that it changed.
    bool becoming_mapped = !paged_vmo();

    if (auto result = EnsureCreatePagedVmo(data_.size(), ZX_VMO_TRAP_DIRTY); result.is_error())
      return result.error_value();

    if (zx_status_t status = paged_vmo().create_child(ZX_VMO_CHILD_SLICE, 0, data_.size(), out_vmo);
        status != ZX_OK)
      return status;
    DidClonePagedVmo();

    if (becoming_mapped)
      shared_->SignalVmoPresenceChanged(true);
    return ZX_OK;
  }

  // Allows tests to force-free the underlying VMO, even if it has mappings.
  void ForceFreePagedVmo() {
    // Free pager_ref outside the lock.
    fbl::RefPtr<fs::Vnode> pager_ref;
    {
      std::lock_guard lock(mutex_);

      if (!shared_->GetVmoPresent())
        return;  // Already gone, nothing to do.

      pager_ref = FreePagedVmo();
      shared_->SignalVmoPresenceChanged(false);
    }
  }

 protected:
  void OnNoPagedVmoClones() override __TA_REQUIRES(mutex_) {
    PagedVnode::OnNoPagedVmoClones();  // Do normal behavior of releasing the VMO.
    shared_->SignalVmoPresenceChanged(false);
  }

 private:
  friend fbl::RefPtr<PagingTestFile>;
  friend fbl::internal::MakeRefCountedHelper<PagingTestFile>;

  PagingTestFile(PagedVfs& vfs, std::shared_ptr<SharedFileState> shared, std::vector<uint8_t> data)
      : PagedVnode(vfs), shared_(std::move(shared)), data_(std::move(data)) {}

  ~PagingTestFile() override = default;

  std::shared_ptr<SharedFileState> shared_;
  std::vector<uint8_t> data_;
  zx_status_t vmo_read_status_ = ZX_OK;
  zx_status_t vmo_dirty_status_ = ZX_OK;
};

// This file has many pages and end in a non-page-boundary.
const char kFile1Name[] = "file1";
constexpr size_t kFile1Size = 4096 * 17 + 87;

// This file is the one that always reports errors.
const char kFileErrName[] = "file_err";
const char kFileDirtyErrName[] = "file_dirty_err";

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

  ~PagingTest() override {
    // Tear down the VFS asynchronously.
    if (vfs_) {
      sync_completion_t completion;
      vfs_->Shutdown([&completion](zx_status_t status) {
        EXPECT_EQ(status, ZX_OK);
        sync_completion_signal(&completion);
      });
      sync_completion_wait(&completion, zx::time::infinite().get());
      vfs_->TearDown();
    }

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
    file1_ = fbl::MakeRefCounted<PagingTestFile>(*vfs_, file1_shared_, file1_contents_);
    root_->AddEntry(kFile1Name, file1_);

    file_err_shared_ = std::make_shared<SharedFileState>();
    file_err_ = fbl::MakeRefCounted<PagingTestFile>(*vfs_, file_err_shared_, file1_contents_);
    file_err_->set_read_status(ZX_ERR_IO_DATA_INTEGRITY);
    root_->AddEntry(kFileErrName, file_err_);

    file_dirty_err_shared_ = std::make_shared<SharedFileState>();
    file_dirty_err_ =
        fbl::MakeRefCounted<PagingTestFile>(*vfs_, file_dirty_err_shared_, file1_contents_);
    file_dirty_err_->set_dirty_status(ZX_ERR_IO_DATA_INTEGRITY);
    root_->AddEntry(kFileDirtyErrName, file_dirty_err_);

    // Connect to the root.
    zx::result directory_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_OK(directory_endpoints.status_value());
    vfs_->ServeDirectory(root_, std::move(directory_endpoints->server));

    // Convert to an FD.
    int root_dir_fd = -1;
    EXPECT_OK(fdio_fd_create(directory_endpoints->client.TakeChannel().release(), &root_dir_fd));

    return root_dir_fd;
  }

 protected:
  std::unique_ptr<PagedVfs> vfs_;

  std::shared_ptr<SharedFileState> file1_shared_;
  std::shared_ptr<SharedFileState> file_err_shared_;
  std::shared_ptr<SharedFileState> file_dirty_err_shared_;
  std::vector<uint8_t> file1_contents_;

  fbl::RefPtr<PagingTestFile> file1_;
  fbl::RefPtr<PagingTestFile> file_err_;
  fbl::RefPtr<PagingTestFile> file_dirty_err_;

 private:
  // The VFS needs to run on a separate thread to handle the FIDL requests from the test because
  // the FDIO calls from the main thread are blocking.
  void VfsThreadProc() { vfs_loop_.Run(); }

  // This test requires two threads because the main thread needs to make blocking calls that are
  // serviced by the VFS on the background thread.
  async::Loop main_loop_;
  std::unique_ptr<std::thread> vfs_thread_;
  async::Loop vfs_loop_;

  fbl::RefPtr<PseudoDir> root_;
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
  EXPECT_FALSE(file1_->HasClones());
  EXPECT_EQ(0u, vfs_->GetRegisteredPagedVmoCount());

  // Gets the VMO for file1, it should now have a VMO.
  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, fdio_get_vmo_exact(file1_fd.get(), vmo.reset_and_get_address()));
  ASSERT_TRUE(file1_shared_->WaitForChangedVmoPresence());
  EXPECT_TRUE(file1_->HasClones());
  EXPECT_EQ(1u, vfs_->GetRegisteredPagedVmoCount());

  // Map the data and validate the result can be read.
  zx_vaddr_t mapped_addr = 0;
  size_t mapped_len = RoundUp<uint64_t>(kFile1Size, zx_system_get_page_size());
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
  EXPECT_TRUE(file1_->HasClones());

  // Unmap the memory. This should notify the vnode which should free its vmo_ reference.
  ASSERT_EQ(ZX_OK, zx::vmar::root_self()->unmap(mapped_addr, mapped_len));
  ASSERT_FALSE(file1_shared_->WaitForChangedVmoPresence());
  EXPECT_FALSE(file1_->HasClones());
  EXPECT_EQ(0u, vfs_->GetRegisteredPagedVmoCount());
}

TEST_F(PagingTest, VmoRead) {
  fbl::unique_fd root_dir_fd(CreateVfs(1));
  ASSERT_TRUE(root_dir_fd);

  // Open file1 and get the VMO.
  fbl::unique_fd file1_fd(openat(root_dir_fd.get(), kFile1Name, 0, S_IRWXU));
  ASSERT_TRUE(file1_fd);
  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, fdio_get_vmo_exact(file1_fd.get(), vmo.reset_and_get_address()));

  // Test that zx_vmo_read works on the file's VMO.
  std::vector<uint8_t> read;
  read.resize(kFile1Size);
  ASSERT_EQ(ZX_OK, vmo.read(read.data(), 0, kFile1Size));
  for (size_t i = 0; i < kFile1Size; i++) {
    ASSERT_EQ(read[i], file1_contents_[i]);
  }
}

// Tests that read errors are propagated. This uses zx_vmo_read so we can get the error without
// segfaulting. Since we're not actually trying to test the kernel's delivery of paging errors, this
// is enough for the VFS paging behavior.
TEST_F(PagingTest, ReadError) {
  fbl::unique_fd root_dir_fd(CreateVfs(1));
  ASSERT_TRUE(root_dir_fd);

  // Open the "error" file and get the VMO.
  fbl::unique_fd file_err_fd(openat(root_dir_fd.get(), kFileErrName, 0, S_IRWXU));
  ASSERT_TRUE(file_err_fd);
  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, fdio_get_vmo_exact(file_err_fd.get(), vmo.reset_and_get_address()));

  // All reads should be errors.
  uint8_t buf[8];
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, vmo.read(buf, 0, std::size(buf)));
}

TEST_F(PagingTest, Write) {
  fbl::unique_fd root_dir_fd(CreateVfs(1));
  ASSERT_TRUE(root_dir_fd);

  fbl::unique_fd file1_fd(openat(root_dir_fd.get(), kFile1Name, O_RDWR, S_IRWXU));
  ASSERT_TRUE(file1_fd);

  // With no VMO requests, there should be no mappings of the VMO in the file.
  ASSERT_FALSE(file1_shared_->GetVmoPresent());
  EXPECT_FALSE(file1_->HasClones());
  EXPECT_EQ(0u, vfs_->GetRegisteredPagedVmoCount());

  // Gets the VMO for file1, it should now have a VMO.
  zx::vmo vmo;
  // TODO: Add fdio_get_vmo_write() to fdio
  fdio_t* io = fdio_unsafe_fd_to_io(static_cast<int>(file1_fd.get()));
  ASSERT_EQ(ZX_OK, zxio_vmo_get(&io->zxio_storage().io, ZXIO_VMO_READ | ZXIO_VMO_WRITE,
                                vmo.reset_and_get_address()));
  fdio_unsafe_release(io);
  ASSERT_TRUE(file1_shared_->WaitForChangedVmoPresence());
  EXPECT_TRUE(file1_->HasClones());
  EXPECT_EQ(1u, vfs_->GetRegisteredPagedVmoCount());

  // Map the data and validate the result can be read.
  zx_vaddr_t mapped_addr = 0;
  size_t mapped_len = RoundUp<uint64_t>(kFile1Size, zx_system_get_page_size());
  ASSERT_EQ(ZX_OK, zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                                              mapped_len, &mapped_addr));
  ASSERT_TRUE(mapped_addr);

  // Clear the VMO so the code below also validates that the mapped memory works even when the
  // VMO is freed. The mapping stores an implicit reference to the vmo.
  vmo.reset();

  std::vector<uint8_t> write_contents;
  write_contents.resize(kFile1Size);
  // Generate write contents pattern
  constexpr uint8_t kMaxByte = 251;
  uint8_t cur = 6;
  for (size_t i = 0; i < kFile1Size; i++) {
    if (cur >= kMaxByte)
      cur = 0;
    write_contents[i] = cur;
    cur++;
  }

  // Write to mmaped memory. This memory access trigger VmoDirty().
  uint8_t* mapped = reinterpret_cast<uint8_t*>(mapped_addr);
  for (size_t i = 0; i < kFile1Size; i++) {
    mapped[i] = write_contents[i];
  }

  // The vmo should still be valid.
  ASSERT_TRUE(file1_shared_->GetVmoPresent());
  EXPECT_TRUE(file1_->HasClones());

  // Mmap to another adderss space and verify data in mmaped memory.
  io = fdio_unsafe_fd_to_io(static_cast<int>(file1_fd.get()));
  ASSERT_EQ(ZX_OK,
            zxio_vmo_get(&io->zxio_storage().io, ZXIO_VMO_READ, vmo.reset_and_get_address()));
  fdio_unsafe_release(io);

  // Map the data and validate the result can be read.
  zx_vaddr_t mapped_addr_2 = 0;
  ASSERT_EQ(ZX_OK,
            zx::vmar::root_self()->map(ZX_VM_PERM_READ, 0, vmo, 0, mapped_len, &mapped_addr_2));
  ASSERT_TRUE(mapped_addr_2);
  ASSERT_NE(mapped_addr, mapped_addr_2);
  vmo.reset();

  const uint8_t* mapped_2 = reinterpret_cast<const uint8_t*>(mapped_addr_2);
  for (size_t i = 0; i < kFile1Size; i++) {
    ASSERT_EQ(mapped_2[i], write_contents[i]);
  }

  // Unmap the memory. This should notify the vnode which should free its vmo_ reference.
  ASSERT_EQ(ZX_OK, zx::vmar::root_self()->unmap(mapped_addr, mapped_len));
  ASSERT_EQ(ZX_OK, zx::vmar::root_self()->unmap(mapped_addr_2, mapped_len));
  ASSERT_FALSE(file1_shared_->WaitForChangedVmoPresence());
  EXPECT_FALSE(file1_->HasClones());
  EXPECT_EQ(0u, vfs_->GetRegisteredPagedVmoCount());
}

TEST_F(PagingTest, VmoDirty) {
  fbl::unique_fd root_dir_fd(CreateVfs(1));
  ASSERT_TRUE(root_dir_fd);

  // Open file1 and get the VMO.
  fbl::unique_fd file1_fd(openat(root_dir_fd.get(), kFile1Name, O_RDWR, S_IRWXU));
  ASSERT_TRUE(file1_fd);
  zx::vmo vmo;
  // TODO: Add fdio_get_vmo_write() to fdio
  fdio_t* io = fdio_unsafe_fd_to_io(static_cast<int>(file1_fd.get()));
  ASSERT_EQ(ZX_OK, zxio_vmo_get(&io->zxio_storage().io, ZXIO_VMO_READ | ZXIO_VMO_WRITE,
                                vmo.reset_and_get_address()));
  fdio_unsafe_release(io);

  // Test that zx_vmo_write works on the file's VMO.
  std::vector<uint8_t> write_contents;
  write_contents.resize(kFile1Size);
  // Generate write contents pattern
  constexpr uint8_t kMaxByte = 251;
  uint8_t cur = 6;
  for (size_t i = 0; i < kFile1Size; i++) {
    if (cur >= kMaxByte)
      cur = 0;
    write_contents[i] = cur;
    cur++;
  }
  ASSERT_EQ(ZX_OK, vmo.write(write_contents.data(), 0, kFile1Size));

  // Verify file contents
  std::vector<uint8_t> read;
  read.resize(kFile1Size);
  ASSERT_EQ(ZX_OK, vmo.read(read.data(), 0, kFile1Size));
  for (size_t i = 0; i < kFile1Size; i++) {
    ASSERT_EQ(read[i], write_contents[i]);
  }
}

TEST_F(PagingTest, WriteError) {
  fbl::unique_fd root_dir_fd(CreateVfs(1));
  ASSERT_TRUE(root_dir_fd);

  // Open the "error" file and get the VMO.
  fbl::unique_fd file_err_fd(openat(root_dir_fd.get(), kFileDirtyErrName, O_RDWR, S_IRWXU));
  ASSERT_TRUE(file_err_fd);
  zx::vmo vmo;
  // TODO: Add fdio_get_vmo_write() to fdio
  fdio_t* io = fdio_unsafe_fd_to_io(static_cast<int>(file_err_fd.get()));
  ASSERT_EQ(ZX_OK, zxio_vmo_get(&io->zxio_storage().io, ZXIO_VMO_READ | ZXIO_VMO_WRITE,
                                vmo.reset_and_get_address()));
  fdio_unsafe_release(io);
  // All writes should be errors.
  uint8_t buf[8];
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, vmo.write(buf, 0, std::size(buf)));
}

TEST_F(PagingTest, FreeWhileClonesExist) {
  fbl::unique_fd root_dir_fd(CreateVfs(1));
  ASSERT_TRUE(root_dir_fd);

  // Open file1 and get the VMO.
  fbl::unique_fd file1_fd(openat(root_dir_fd.get(), kFile1Name, 0, S_IRWXU));
  ASSERT_TRUE(file1_fd);
  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, fdio_get_vmo_exact(file1_fd.get(), vmo.reset_and_get_address()));

  // Force releasing the VMO even though a clone still exists.
  file1_->ForceFreePagedVmo();

  // After detaching the VMO, it should report there is no VMO and reads from it should fail.
  EXPECT_FALSE(file1_->HasClones());
  uint8_t read_byte;
  ASSERT_EQ(ZX_ERR_BAD_STATE, vmo.read(&read_byte, 0, 1));
}

// TODO(bug 51111):
//  - Test closing a file frees the PagedVnode object.
//  - Test multiple threads (deliberately hang one to make sure we can service another request).

}  // namespace fs
