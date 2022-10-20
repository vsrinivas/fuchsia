// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/vfs/cpp/pseudo_file.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "fuchsia/io/cpp/fidl.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace {

class FileWrapper {
 public:
  const std::string& buffer() { return buffer_; }

  vfs::PseudoFile* file() { return file_.get(); }

  static FileWrapper CreateReadWriteFile(std::string initial_str, size_t capacity,
                                         bool start_loop = true) {
    return FileWrapper(true, std::move(initial_str), capacity, start_loop);
  }

  static FileWrapper CreateReadOnlyFile(std::string initial_str, bool start_loop = true) {
    size_t length = initial_str.length();
    return FileWrapper(false, std::move(initial_str), length, start_loop);
  }

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

  async::Loop& loop() { return loop_; }

 private:
  FileWrapper(bool write_allowed, std::string initial_str, size_t capacity, bool start_loop)
      : buffer_(std::move(initial_str)), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    auto readFn = [this](std::vector<uint8_t>* output, size_t max_file_size) {
      output->resize(buffer_.length());
      std::copy(buffer_.begin(), buffer_.end(), output->begin());
      return ZX_OK;
    };

    vfs::PseudoFile::WriteHandler writeFn;
    if (write_allowed) {
      writeFn = [this](std::vector<uint8_t> input) {
        std::string str(input.size(), 0);
        std::copy(input.begin(), input.end(), str.begin());
        buffer_ = std::move(str);
        return ZX_OK;
      };
    }

    file_ = std::make_unique<vfs::PseudoFile>(capacity, std::move(readFn), std::move(writeFn));
    if (start_loop) {
      loop_.StartThread("vfs test thread");
    }
  }

  std::unique_ptr<vfs::PseudoFile> file_;
  std::string buffer_;
  async::Loop loop_;
};

class PseudoFileTest : public gtest::RealLoopFixture {
 protected:
  void AssertOpen(vfs::internal::Node* node, async_dispatcher_t* dispatcher,
                  fuchsia::io::OpenFlags flags, zx_status_t expected_status,
                  bool test_on_open_event = true) {
    fuchsia::io::NodePtr node_ptr;
    if (test_on_open_event) {
      flags |= fuchsia::io::OpenFlags::DESCRIBE;
    }
    EXPECT_EQ(expected_status, node->Serve(flags, node_ptr.NewRequest().TakeChannel(), dispatcher));

    if (test_on_open_event) {
      bool on_open_called = false;
      node_ptr.events().OnOpen = [&](zx_status_t status,
                                     std::unique_ptr<fuchsia::io::NodeInfoDeprecated> info) {
        EXPECT_FALSE(on_open_called);  // should be called only once
        on_open_called = true;
        EXPECT_EQ(expected_status, status);
        if (expected_status == ZX_OK) {
          ASSERT_NE(info.get(), nullptr);
          EXPECT_TRUE(info->is_file());
        } else {
          EXPECT_EQ(info.get(), nullptr);
        }
      };

      RunLoopUntil([&]() { return on_open_called; });
    }
  }

  static fuchsia::io::FileSyncPtr OpenReadWrite(vfs::internal::Node* node,
                                                async_dispatcher_t* dispatcher) {
    return OpenFile(node,
                    fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE,
                    dispatcher);
  }

  static fuchsia::io::FileSyncPtr OpenRead(vfs::internal::Node* node,
                                           async_dispatcher_t* dispatcher) {
    return OpenFile(node, fuchsia::io::OpenFlags::RIGHT_READABLE, dispatcher);
  }

  static fuchsia::io::FileSyncPtr OpenFile(vfs::internal::Node* node, fuchsia::io::OpenFlags flags,
                                           async_dispatcher_t* dispatcher) {
    fuchsia::io::FileSyncPtr ptr;
    node->Serve(flags, ptr.NewRequest().TakeChannel(), dispatcher);
    return ptr;
  }

  static void AssertWriteAt(fuchsia::io::FileSyncPtr& file, const std::string& str, uint64_t offset,
                            zx_status_t expected_status = ZX_OK, int expected_actual = -1) {
    std::vector<uint8_t> buffer;
    buffer.resize(str.length());
    std::copy(str.begin(), str.end(), buffer.begin());
    fuchsia::io::File_WriteAt_Result result;
    ASSERT_EQ(ZX_OK, file->WriteAt(buffer, offset, &result));
    if (expected_status == ZX_OK) {
      ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
      ASSERT_EQ(expected_actual == -1 ? str.length() : expected_actual,
                result.response().actual_count);
    } else {
      ASSERT_TRUE(result.is_err());
      ASSERT_EQ(expected_status, result.err());
    }
  }

  static void AssertWrite(fuchsia::io::FileSyncPtr& file, const std::string& str,
                          zx_status_t expected_status = ZX_OK, int expected_actual = -1) {
    std::vector<uint8_t> buffer;
    buffer.resize(str.length());
    std::copy(str.begin(), str.end(), buffer.begin());
    fuchsia::io::Writable_Write_Result result;
    ASSERT_EQ(ZX_OK, file->Write(buffer, &result));
    if (expected_status == ZX_OK) {
      ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
      ASSERT_EQ(expected_actual == -1 ? str.length() : expected_actual,
                result.response().actual_count);
    } else {
      ASSERT_TRUE(result.is_err());
      ASSERT_EQ(expected_status, result.err());
    }
  }

  static void AssertReadAt(fuchsia::io::FileSyncPtr& file, uint64_t offset, uint64_t count,
                           const std::string& expected_str, zx_status_t expected_status = ZX_OK) {
    fuchsia::io::File_ReadAt_Result result;
    ASSERT_EQ(ZX_OK, file->ReadAt(count, offset, &result));
    if (expected_status == ZX_OK) {
      ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
      const std::vector<uint8_t>& data = result.response().data;
      ASSERT_EQ(expected_str,
                std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
    } else {
      ASSERT_TRUE(result.is_err());
      ASSERT_EQ(expected_status, result.err());
    }
  }

  static void AssertRead(fuchsia::io::FileSyncPtr& file, uint64_t count,
                         const std::string& expected_str, zx_status_t expected_status = ZX_OK) {
    fuchsia::io::Readable_Read_Result result;
    ASSERT_EQ(ZX_OK, file->Read(count, &result));
    if (expected_status == ZX_OK) {
      ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
      const std::vector<uint8_t>& data = result.response().data;
      ASSERT_EQ(expected_str,
                std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
    } else {
      ASSERT_TRUE(result.is_err());
      ASSERT_EQ(expected_status, result.err());
    }
  }

  static void AssertResize(fuchsia::io::FileSyncPtr& file, uint64_t count,
                           zx_status_t expected_status = ZX_OK) {
    fuchsia::io::File_Resize_Result result;
    ASSERT_EQ(ZX_OK, file->Resize(count, &result));
    if (expected_status == ZX_OK) {
      ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    } else {
      ASSERT_TRUE(result.is_err());
      ASSERT_EQ(expected_status, result.err());
    }
  }

  static void AssertSeek(fuchsia::io::FileSyncPtr& file, int64_t offset,
                         fuchsia::io::SeekOrigin origin, uint64_t expected_offset,
                         zx_status_t expected_status = ZX_OK) {
    fuchsia::io::File_Seek_Result result;
    ASSERT_EQ(ZX_OK, file->Seek(origin, offset, &result));
    if (expected_status == ZX_OK) {
      ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
      ASSERT_EQ(expected_offset, result.response().offset_from_start);
    } else {
      ASSERT_TRUE(result.is_err());
      ASSERT_EQ(expected_status, result.err());
    }
  }

  static void CloseFile(fuchsia::io::FileSyncPtr& file, zx_status_t expected_status = ZX_OK) {
    fuchsia::unknown::Closeable_Close_Result result;
    ASSERT_EQ(ZX_OK, file->Close(&result));
    if (expected_status == ZX_OK) {
      ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    } else {
      ASSERT_TRUE(result.is_err());
      EXPECT_EQ(expected_status, result.err());
    }
  }

  void AssertFileWrapperState(FileWrapper& file_wrapper, const std::string& expected_str) {
    RunLoopUntil([&]() { return file_wrapper.buffer() == expected_str; });
  }

  static int OpenAsFD(vfs::internal::Node* node, async_dispatcher_t* dispatcher) {
    zx::channel local, remote;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
    EXPECT_EQ(ZX_OK, node->Serve(fuchsia::io::OpenFlags::RIGHT_READABLE |
                                     fuchsia::io::OpenFlags::RIGHT_WRITABLE,
                                 std::move(remote), dispatcher));
    int fd = -1;
    EXPECT_EQ(ZX_OK, fdio_fd_create(local.release(), &fd));
    return fd;
  }
};

TEST_F(PseudoFileTest, ServeOnInValidFlagsForReadWriteFile) {
  auto file_wrapper = FileWrapper::CreateReadWriteFile("test_str", 100, false);
  {
    SCOPED_TRACE("OPEN_FLAG_DIRECTORY");
    AssertOpen(file_wrapper.file(), dispatcher(), fuchsia::io::OpenFlags::DIRECTORY,
               ZX_ERR_NOT_DIR);
  }
  fuchsia::io::OpenFlags not_allowed_flags[] = {fuchsia::io::OpenFlags::CREATE,
                                                fuchsia::io::OpenFlags::CREATE_IF_ABSENT,
                                                fuchsia::io::OpenFlags::APPEND};
  for (auto not_allowed_flag : not_allowed_flags) {
    SCOPED_TRACE(std::to_string(static_cast<uint32_t>(not_allowed_flag)));
    AssertOpen(file_wrapper.file(), dispatcher(), not_allowed_flag, ZX_ERR_NOT_SUPPORTED);
  }
}

TEST_F(PseudoFileTest, ServeOnInValidFlagsForReadOnlyFile) {
  auto file_wrapper = FileWrapper::CreateReadOnlyFile("test_str");
  {
    SCOPED_TRACE("OPEN_FLAG_DIRECTORY");
    AssertOpen(file_wrapper.file(), dispatcher(), fuchsia::io::OpenFlags::DIRECTORY,
               ZX_ERR_NOT_DIR);
  }
  fuchsia::io::OpenFlags not_allowed_flags[] = {
      fuchsia::io::OpenFlags::CREATE, fuchsia::io::OpenFlags::CREATE_IF_ABSENT,
      fuchsia::io::OpenFlags::RIGHT_WRITABLE, fuchsia::io::OpenFlags::TRUNCATE,
      fuchsia::io::OpenFlags::APPEND};
  for (auto not_allowed_flag : not_allowed_flags) {
    SCOPED_TRACE(std::to_string(static_cast<uint32_t>(not_allowed_flag)));
    AssertOpen(file_wrapper.file(), dispatcher(), not_allowed_flag, ZX_ERR_NOT_SUPPORTED);
  }
}

TEST_F(PseudoFileTest, ServeOnValidFlagsForReadWriteFile) {
  auto file_wrapper = FileWrapper::CreateReadWriteFile("test_str", 100, false);
  fuchsia::io::OpenFlags allowed_flags[] = {
      fuchsia::io::OpenFlags::RIGHT_READABLE, fuchsia::io::OpenFlags::RIGHT_WRITABLE,
      fuchsia::io::OpenFlags::NODE_REFERENCE, fuchsia::io::OpenFlags::TRUNCATE,
      fuchsia::io::OpenFlags::NOT_DIRECTORY};
  for (auto allowed_flag : allowed_flags) {
    SCOPED_TRACE(std::to_string(static_cast<uint32_t>(allowed_flag)));
    AssertOpen(file_wrapper.file(), dispatcher(), allowed_flag, ZX_OK);
  }
}

TEST_F(PseudoFileTest, ServeOnValidFlagsForReadOnlyFile) {
  auto file_wrapper = FileWrapper::CreateReadOnlyFile("test_str", false);
  fuchsia::io::OpenFlags allowed_flags[] = {fuchsia::io::OpenFlags::RIGHT_READABLE,
                                            fuchsia::io::OpenFlags::NODE_REFERENCE};
  for (auto allowed_flag : allowed_flags) {
    SCOPED_TRACE(std::to_string(static_cast<uint32_t>(allowed_flag)));
    AssertOpen(file_wrapper.file(), dispatcher(), allowed_flag, ZX_OK);
  }
}

TEST_F(PseudoFileTest, Simple) {
  auto file_wrapper = FileWrapper::CreateReadWriteFile("test_str", 100);

  int fd = OpenAsFD(file_wrapper.file(), file_wrapper.dispatcher());
  ASSERT_LE(0, fd);

  char buffer[1024];
  memset(buffer, 0, sizeof(buffer));
  ASSERT_EQ(5, pread(fd, buffer, 5, 0));
  EXPECT_STREQ("test_", buffer);

  ASSERT_EQ(4, write(fd, "abcd", 4));
  ASSERT_EQ(5, pread(fd, buffer, 5, 0));
  EXPECT_STREQ("abcd_", buffer);

  ASSERT_GE(0, close(fd));
  file_wrapper.loop().RunUntilIdle();

  AssertFileWrapperState(file_wrapper, "abcd_str");
}

TEST_F(PseudoFileTest, WriteAt) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  AssertWriteAt(file, "was", 5);

  const std::string updated_str = "this wasa test string";
  // confirm by reading
  AssertRead(file, str.length(), updated_str);

  // make sure file was not updated before connection was closed.
  ASSERT_EQ(file_wrapper.buffer(), str);

  CloseFile(file);

  AssertFileWrapperState(file_wrapper, updated_str);
}

TEST_F(PseudoFileTest, MultipleWriteAt) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  AssertWriteAt(file, "was", 5);

  AssertWriteAt(file, "tests", 10);

  const std::string updated_str = "this wasa testsstring";
  // confirm by reading
  AssertRead(file, str.length(), updated_str);

  // make sure file was not updated before connection was closed.
  ASSERT_EQ(file_wrapper.buffer(), str);

  CloseFile(file);

  AssertFileWrapperState(file_wrapper, updated_str);
}

TEST_F(PseudoFileTest, ReadAt) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  AssertReadAt(file, 5, 10, str.substr(5, 10));

  // try one more
  AssertReadAt(file, 15, 5, str.substr(15, 5));
}

TEST_F(PseudoFileTest, Read) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  AssertRead(file, 10, str.substr(0, 10));

  // offset should have moved
  AssertRead(file, 10, str.substr(10, 10));
}

TEST_F(PseudoFileTest, GetAttr) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  zx_status_t status;
  fuchsia::io::NodeAttributes attr;
  ASSERT_EQ(ZX_OK, file->GetAttr(&status, &attr));
  ASSERT_EQ(ZX_OK, status);
  ASSERT_NE(0u, attr.mode & fuchsia::io::MODE_TYPE_FILE);
  ASSERT_EQ(21u, attr.content_size);
}

TEST_F(PseudoFileTest, Write) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  AssertWrite(file, "It");

  // offset should have moved
  AssertWrite(file, " is");

  const std::string updated_str = "It isis a test string";

  AssertReadAt(file, 0, 100, updated_str);

  // make sure file was not updated before connection was closed.
  ASSERT_EQ(file_wrapper.buffer(), str);

  CloseFile(file);

  // make sure file was updated
  AssertFileWrapperState(file_wrapper, updated_str);
}

TEST_F(PseudoFileTest, Truncate) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  AssertResize(file, 10);

  AssertRead(file, 100, str.substr(0, 10));

  // make sure file was not updated before connection was closed.
  ASSERT_EQ(file_wrapper.buffer(), str);

  CloseFile(file);

  // make sure file was updated
  AssertFileWrapperState(file_wrapper, str.substr(0, 10));
}

TEST_F(PseudoFileTest, SeekFromStart) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadOnlyFile(str);
  auto file = OpenRead(file_wrapper.file(), file_wrapper.dispatcher());

  AssertSeek(file, 5, fuchsia::io::SeekOrigin::START, 5);

  AssertRead(file, 100, str.substr(5));
}

TEST_F(PseudoFileTest, SeekFromCurent) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadOnlyFile(str);
  auto file = OpenRead(file_wrapper.file(), file_wrapper.dispatcher());

  AssertSeek(file, 5, fuchsia::io::SeekOrigin::START, 5);

  AssertSeek(file, 10, fuchsia::io::SeekOrigin::CURRENT, 15);

  AssertRead(file, 100, str.substr(15));
}

TEST_F(PseudoFileTest, SeekFromEnd) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadOnlyFile(str);
  auto file = OpenRead(file_wrapper.file(), file_wrapper.dispatcher());

  AssertSeek(file, -2, fuchsia::io::SeekOrigin::END, str.length() - 2);

  AssertRead(file, 100, str.substr(str.length() - 2));
}

TEST_F(PseudoFileTest, SeekFromEndWith0Offset) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadOnlyFile(str);
  auto file = OpenRead(file_wrapper.file(), file_wrapper.dispatcher());

  AssertSeek(file, 0, fuchsia::io::SeekOrigin::END, str.length());

  AssertRead(file, 100, "");
}

TEST_F(PseudoFileTest, SeekFailsIfOffsetMoreThanCapacity) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadOnlyFile(str);
  auto file = OpenRead(file_wrapper.file(), file_wrapper.dispatcher());

  AssertSeek(file, 1, fuchsia::io::SeekOrigin::END, 0, ZX_ERR_OUT_OF_RANGE);

  // make sure offset did not change
  AssertRead(file, 100, str);
}

TEST_F(PseudoFileTest, WriteafterEndOfFile) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  AssertSeek(file, 5, fuchsia::io::SeekOrigin::END, str.length() + 5);

  AssertWrite(file, "is");

  auto updated_str = str;
  updated_str.append(5, 0).append("is");

  AssertReadAt(file, 0, 100, updated_str);

  // make sure file was not updated before connection was closed.
  ASSERT_EQ(file_wrapper.buffer(), str);

  CloseFile(file);

  // make sure file was updated
  AssertFileWrapperState(file_wrapper, updated_str);
}

TEST_F(PseudoFileTest, WriteFailsForReadOnly) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenRead(file_wrapper.file(), file_wrapper.dispatcher());

  AssertWrite(file, "is", ZX_ERR_BAD_HANDLE, 0);

  CloseFile(file);

  // make sure file was not updated
  AssertFileWrapperState(file_wrapper, str);
}

TEST_F(PseudoFileTest, WriteAtFailsForReadOnly) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenRead(file_wrapper.file(), file_wrapper.dispatcher());

  AssertWriteAt(file, "is", 0, ZX_ERR_BAD_HANDLE, 0);

  CloseFile(file);

  // make sure file was not updated
  AssertFileWrapperState(file_wrapper, str);
}

TEST_F(PseudoFileTest, TruncateFailsForReadOnly) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenRead(file_wrapper.file(), file_wrapper.dispatcher());

  AssertResize(file, 10, ZX_ERR_BAD_HANDLE);

  CloseFile(file);

  // make sure file was not updated
  AssertFileWrapperState(file_wrapper, str);
}

TEST_F(PseudoFileTest, ReadAtFailsForWriteOnly) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenFile(file_wrapper.file(), fuchsia::io::OpenFlags::RIGHT_WRITABLE,
                       file_wrapper.dispatcher());

  AssertReadAt(file, 0, 10, "", ZX_ERR_BAD_HANDLE);
}

TEST_F(PseudoFileTest, ReadFailsForWriteOnly) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenFile(file_wrapper.file(), fuchsia::io::OpenFlags::RIGHT_WRITABLE,
                       file_wrapper.dispatcher());

  AssertRead(file, 10, "", ZX_ERR_BAD_HANDLE);
}

TEST_F(PseudoFileTest, CapacityisSameAsFileContentSize) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, str.length());
  auto file = OpenFile(file_wrapper.file(), fuchsia::io::OpenFlags::RIGHT_READABLE,
                       file_wrapper.dispatcher());

  AssertRead(file, str.length(), str);
}

TEST_F(PseudoFileTest, OpenFailsForOverFlowingFile) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, str.length() - 1);
  AssertOpen(file_wrapper.file(), file_wrapper.dispatcher(), fuchsia::io::OpenFlags::RIGHT_READABLE,
             ZX_ERR_FILE_BIG);
}

TEST_F(PseudoFileTest, CantReadNodeReferenceFile) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenFile(file_wrapper.file(), fuchsia::io::OpenFlags::NODE_REFERENCE,
                       file_wrapper.dispatcher());
  // make sure node reference was opened
  zx_status_t status;
  fuchsia::io::NodeAttributes attr;
  ASSERT_EQ(ZX_OK, file->GetAttr(&status, &attr));
  ASSERT_EQ(ZX_OK, status);
  ASSERT_NE(0u, attr.mode | fuchsia::io::MODE_TYPE_FILE);

  fuchsia::io::Readable_Read_Result result;
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, file->Read(100, &result));
}

TEST_F(PseudoFileTest, CanCloneFileConnectionAndReadAndWrite) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  fuchsia::io::FileSyncPtr cloned_file;
  ASSERT_EQ(ZX_OK, file->Clone(fuchsia::io::OpenFlags::CLONE_SAME_RIGHTS,
                               fidl::InterfaceRequest<fuchsia::io::Node>(
                                   cloned_file.NewRequest().TakeChannel())));

  CloseFile(file);

  AssertWrite(cloned_file, "It");

  const std::string updated_str = "Itis is a test string";

  AssertReadAt(cloned_file, 0, 100, updated_str);

  // make sure file was not updated before connection was closed.
  ASSERT_EQ(file_wrapper.buffer(), str);

  CloseFile(cloned_file);

  // make sure file was updated
  AssertFileWrapperState(file_wrapper, updated_str);
}

TEST_F(PseudoFileTest, NodeReferenceIsClonedAsNodeReference) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapper::CreateReadWriteFile(str, 100);
  auto file = OpenFile(file_wrapper.file(), fuchsia::io::OpenFlags::NODE_REFERENCE,
                       file_wrapper.dispatcher());

  fuchsia::io::FileSyncPtr cloned_file;
  ASSERT_EQ(ZX_OK, file->Clone(fuchsia::io::OpenFlags::CLONE_SAME_RIGHTS,
                               fidl::InterfaceRequest<fuchsia::io::Node>(
                                   cloned_file.NewRequest().TakeChannel())));
  CloseFile(file);

  // make sure node reference was opened
  zx_status_t status;
  fuchsia::io::NodeAttributes attr;
  ASSERT_EQ(ZX_OK, cloned_file->GetAttr(&status, &attr));
  ASSERT_EQ(ZX_OK, status);
  ASSERT_NE(0u, attr.mode | fuchsia::io::MODE_TYPE_FILE);

  fuchsia::io::Readable_Read_Result result;
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, cloned_file->Read(100, &result));
}

class FileWrapperWithFailingWriteFn {
 public:
  vfs::PseudoFile* file() { return file_.get(); }

  static FileWrapperWithFailingWriteFn Create(std::string initial_str, size_t capacity,
                                              zx_status_t write_error) {
    return FileWrapperWithFailingWriteFn(std::move(initial_str), capacity, write_error);
  }

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

  async::Loop& loop() { return loop_; }

 private:
  FileWrapperWithFailingWriteFn(std::string initial_str, size_t capacity, zx_status_t write_error)
      : buffer_(std::move(initial_str)), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    auto readFn = [this](std::vector<uint8_t>* output, size_t max_file_size) {
      output->resize(buffer_.length());
      std::copy(buffer_.begin(), buffer_.end(), output->begin());
      return ZX_OK;
    };

    vfs::PseudoFile::WriteHandler writeFn;
    writeFn = [this, write_error](std::vector<uint8_t> input) {
      std::string str(input.size(), 0);
      std::copy(input.begin(), input.end(), str.begin());
      buffer_ = std::move(str);
      return write_error;
    };

    file_ = std::make_unique<vfs::PseudoFile>(capacity, std::move(readFn), std::move(writeFn));
    loop_.StartThread("vfs test thread");
  }

  std::unique_ptr<vfs::PseudoFile> file_;
  std::string buffer_;
  async::Loop loop_;
};

TEST_F(PseudoFileTest, CloseReturnsWriteError) {
  const std::string str = "this is a test string";
  auto file_wrapper = FileWrapperWithFailingWriteFn::Create(str, 100, ZX_ERR_IO);
  auto file = OpenReadWrite(file_wrapper.file(), file_wrapper.dispatcher());

  AssertWrite(file, "It");

  CloseFile(file, ZX_ERR_IO);
}

}  // namespace
