// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

#include <fbl/unique_fd.h>
#include <loader-service/loader-service.h>
#include <zxtest/zxtest.h>

namespace fuchsia = ::llcpp::fuchsia;

// Fill in all the methods with empty impls; we only care about a couple methods
// in tests
class StubFile : public fuchsia::io::File::Interface {
 public:
  StubFile() {}
  virtual ~StubFile() {}
  virtual void Clone(uint32_t flags, ::zx::channel object, CloneCompleter::Sync _completer) {}
  virtual void Close(CloseCompleter::Sync completer) {}
  virtual void Describe(DescribeCompleter::Sync _completer) {}
  virtual void Sync(SyncCompleter::Sync _completer) {}
  virtual void GetAttr(GetAttrCompleter::Sync _completer) {}
  virtual void SetAttr(uint32_t flags, fuchsia::io::NodeAttributes attributes,
                       SetAttrCompleter::Sync _completer) {}
  virtual void Read(uint64_t count, ReadCompleter::Sync _completer) {}
  virtual void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync _completer) {}
  virtual void Write(::fidl::VectorView<uint8_t> data, WriteCompleter::Sync _completer) {}
  virtual void WriteAt(::fidl::VectorView<uint8_t> data, uint64_t offset,
                       WriteAtCompleter::Sync _completer) {}
  virtual void Seek(int64_t offset, fuchsia::io::SeekOrigin start, SeekCompleter::Sync _completer) {
  }
  virtual void Truncate(uint64_t length, TruncateCompleter::Sync _completer) {}
  virtual void GetFlags(GetFlagsCompleter::Sync _completer) {}
  virtual void SetFlags(uint32_t flags, SetFlagsCompleter::Sync _completer) {}
  virtual void GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) {}
};

// Fill in all the methods with empty impls; we only care about a couple methods
// in tests
class StubDirectory : public fuchsia::io::Directory::Interface {
 public:
  StubDirectory() {}
  virtual ~StubDirectory() {}
  virtual void Describe(DescribeCompleter::Sync completer) {}
  virtual void Clone(uint32_t flags, ::zx::channel object, CloneCompleter::Sync _completer) {}
  virtual void Close(CloseCompleter::Sync completer) {}
  virtual void Sync(SyncCompleter::Sync _completer) {}
  virtual void GetAttr(GetAttrCompleter::Sync _completer) {}
  virtual void SetAttr(uint32_t flags, fuchsia::io::NodeAttributes attributes,
                       SetAttrCompleter::Sync _completer) {}
  virtual void Open(uint32_t flags, uint32_t mode, ::fidl::StringView path, ::zx::channel object,
                    OpenCompleter::Sync completer) {}
  virtual void Unlink(::fidl::StringView path, UnlinkCompleter::Sync _completer) {}
  virtual void ReadDirents(uint64_t max_bytes, ReadDirentsCompleter::Sync _completer) {}
  virtual void Rewind(RewindCompleter::Sync _completer) {}
  virtual void GetToken(GetTokenCompleter::Sync _completer) {}
  virtual void Rename(::fidl::StringView src, ::zx::handle dst_parent_token, ::fidl::StringView dst,
                      RenameCompleter::Sync _completer) {}
  virtual void Link(::fidl::StringView src, ::zx::handle dst_parent_token, ::fidl::StringView dst,
                    LinkCompleter::Sync _completer) {}
  virtual void Watch(uint32_t mask, uint32_t options, ::zx::channel watcher,
                     WatchCompleter::Sync _completer) {}
};

TEST(LoaderServiceTest, Create) {
  // make a dispatcher loop on a thread
  async::Loop fs_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(fs_loop.StartThread("fake-filesystem"));

  // make a mock filesystem (directory and contained file) that records:
  // * Open flags
  // * Open path
  // * Open count
  // * GetBuffer flags
  uint32_t last_get_buffer_flags = 0;
  uint32_t last_open_flags = 0;
  uint32_t open_count = 0;
  char* last_opened_path = new char[PATH_MAX + 1];

  class TestFile final : public StubFile {
   public:
    TestFile(uint32_t* get_buffer_flags) : get_buffer_flags_ptr_(get_buffer_flags) {}
    ~TestFile() {}
    void Close(CloseCompleter::Sync completer) override { completer.Reply(ZX_OK); }
    void GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) override {
      *get_buffer_flags_ptr_ = flags;
      zx::vmo vmo;
      zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo);
      fuchsia::mem::Buffer buffer = {};
      buffer.size = 0;
      buffer.vmo = std::move(vmo);
      completer.Reply(ZX_OK, &buffer);
    }
    uint32_t* get_buffer_flags_ptr_;
  };

  class TestDirectory final : public StubDirectory {
   public:
    TestDirectory(async_dispatcher_t* dispatcher, uint32_t* last_open_flags_ptr,
                  uint32_t* open_count_ptr, uint32_t* last_get_buffer_flags_ptr,
                  char* last_opened_path)
        : dispatcher_(dispatcher),
          last_open_flags_ptr_(last_open_flags_ptr),
          open_count_ptr_(open_count_ptr),
          last_get_buffer_flags_ptr_(last_get_buffer_flags_ptr),
          last_opened_path_(last_opened_path) {}
    ~TestDirectory() {}

    void Describe(DescribeCompleter::Sync completer) override {
      fuchsia::io::DirectoryObject obj;
      fuchsia::io::NodeInfo info = fuchsia::io::NodeInfo::WithDirectory(&obj);
      completer.Reply(std::move(info));
    }
    void Close(CloseCompleter::Sync completer) override { completer.Reply(ZX_OK); }
    void Open(uint32_t flags, uint32_t mode, ::fidl::StringView path, ::zx::channel object,
              OpenCompleter::Sync completer) override {
      // Save arguments
      *last_open_flags_ptr_ = flags;
      *open_count_ptr_ += 1;
      memcpy(last_opened_path_, path.data(), path.size());
      last_opened_path_[path.size()] = '\0';

      // Send the OnOpen event on the channel
      fuchsia::io::FileObject obj;
      fuchsia::io::NodeInfo info = fuchsia::io::NodeInfo::WithFile(&obj);
      fuchsia::io::File::SendOnOpenEvent(zx::unowned_channel{object}, ZX_OK, info);

      // Wire object up to a new TestFile instance
      auto file = std::make_unique<TestFile>(last_get_buffer_flags_ptr_);
      ASSERT_OK(fidl::Bind(dispatcher_, std::move(object), std::move(file)));
    }

    async_dispatcher_t* dispatcher_;
    uint32_t* last_open_flags_ptr_;
    uint32_t* open_count_ptr_;
    uint32_t* last_get_buffer_flags_ptr_;
    char* last_opened_path_;
  };

  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  auto directory =
      std::make_unique<TestDirectory>(fs_loop.dispatcher(), &last_open_flags, &open_count,
                                      &last_get_buffer_flags, last_opened_path);
  ASSERT_OK(fidl::Bind(fs_loop.dispatcher(), std::move(server), std::move(directory)));

  // Install channel to that filesystem as an FD
  int raw_fd;
  ASSERT_OK(fdio_fd_create(client.get(), &raw_fd));
  fbl::unique_fd fd(raw_fd);

  // Create loader service with that fd.  It blocks on the FS, so run it on
  // a second new thread.
  async::Loop ldsvc_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(ldsvc_loop.StartThread("loader-service"));

  loader_service_t* service;
  ASSERT_OK(loader_service_create_fd(ldsvc_loop.dispatcher(), fd.release(), &service));

  // use it to load a thing
  zx::channel ldsvc;
  ASSERT_OK(loader_service_connect(service, ldsvc.reset_and_get_address()));

  {
    fidl::StringView lib("a.so");
    auto result = fuchsia::ldsvc::Loader::Call::LoadObject(zx::unowned_channel{ldsvc}, lib);
    // Verify that succeeded and the handle we get back is valid.
    ASSERT_TRUE(result.ok());
    auto& vmo = result->object;
    ASSERT_TRUE(vmo.is_valid());

    // Verify that calls to mock objects had the expected flags
    EXPECT_EQ(1, open_count);
    uint32_t expected_open_flags = fuchsia::io::OPEN_RIGHT_READABLE |
                                   fuchsia::io::OPEN_RIGHT_EXECUTABLE |
                                   fuchsia::io::OPEN_FLAG_DESCRIBE;
    EXPECT_EQ(expected_open_flags, last_open_flags);
    EXPECT_EQ(0, strcmp("lib/a.so", last_opened_path));
    uint32_t expected_get_buffer_flags =
        fuchsia::io::VMO_FLAG_READ | fuchsia::io::VMO_FLAG_EXEC | fuchsia::io::VMO_FLAG_PRIVATE;
    EXPECT_EQ(expected_get_buffer_flags, last_get_buffer_flags);
  }

  // tear down loader service
  loader_service_release(service);
}
