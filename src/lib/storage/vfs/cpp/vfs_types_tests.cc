// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <zircon/errors.h>

#include <type_traits>
#include <utility>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace {

namespace fio = fuchsia_io;

TEST(Rights, ReadOnly) {
  // clang-format off
  EXPECT_TRUE (fs::Rights::ReadOnly().read,    "Bad value for Rights::ReadOnly().read");
  EXPECT_FALSE(fs::Rights::ReadOnly().write,   "Bad value for Rights::ReadOnly().write");
  EXPECT_FALSE(fs::Rights::ReadOnly().execute, "Bad value for Rights::ReadOnly().execute");
  // clang-format on
}

TEST(Rights, WriteOnly) {
  // clang-format off
  EXPECT_FALSE(fs::Rights::WriteOnly().read,    "Bad value for Rights::WriteOnly().read");
  EXPECT_TRUE (fs::Rights::WriteOnly().write,   "Bad value for Rights::WriteOnly().write");
  EXPECT_FALSE(fs::Rights::WriteOnly().execute, "Bad value for Rights::WriteOnly().execute");
  // clang-format on
}

TEST(Rights, ReadWrite) {
  // clang-format off
  EXPECT_TRUE (fs::Rights::ReadWrite().read,    "Bad value for Rights::ReadWrite().read");
  EXPECT_TRUE (fs::Rights::ReadWrite().write,   "Bad value for Rights::ReadWrite().write");
  EXPECT_FALSE(fs::Rights::ReadWrite().execute, "Bad value for Rights::ReadWrite().execute");
  // clang-format on
}

TEST(Rights, ReadExec) {
  // clang-format off
  EXPECT_TRUE (fs::Rights::ReadExec().read,    "Bad value for Rights::ReadExec().read");
  EXPECT_FALSE(fs::Rights::ReadExec().write,   "Bad value for Rights::ReadExec().write");
  EXPECT_TRUE (fs::Rights::ReadExec().execute, "Bad value for Rights::ReadExec().execute");
  // clang-format on
}

TEST(Rights, WriteExec) {
  // clang-format off
  EXPECT_FALSE(fs::Rights::WriteExec().read,    "Bad value for Rights::WriteExec().read");
  EXPECT_TRUE (fs::Rights::WriteExec().write,   "Bad value for Rights::WriteExec().write");
  EXPECT_TRUE (fs::Rights::WriteExec().execute, "Bad value for Rights::WriteExec().execute");
  // clang-format on
}

TEST(Rights, All) {
  // clang-format off
  EXPECT_TRUE (fs::Rights::All().read,    "Bad value for Rights::All().read");
  EXPECT_TRUE (fs::Rights::All().write,   "Bad value for Rights::All().write");
  EXPECT_TRUE (fs::Rights::All().execute, "Bad value for Rights::All().execute");
  // clang-format on
}

class DummyVnode : public fs::Vnode {
 public:
  DummyVnode() = default;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol, fs::Rights,
                                     fs::VnodeRepresentation*) final {
    ZX_PANIC("Unused");
  }
};

#define EXPECT_RESULT_OK(expr) EXPECT_TRUE((expr).is_ok())
#define EXPECT_RESULT_ERROR(error_val, expr) \
  EXPECT_TRUE((expr).is_error());            \
  EXPECT_EQ(error_val, (expr).status_value())

TEST(VnodeConnectionOptions, ValidateOptionsForDirectory) {
  class TestDirectory : public DummyVnode {
   public:
    fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kDirectory; }
  };

  TestDirectory vnode;
  EXPECT_RESULT_OK(vnode.ValidateOptions(
      fs::VnodeConnectionOptions::FromIoV1Flags(fio::wire::OpenFlags::kDirectory)));
  EXPECT_RESULT_ERROR(ZX_ERR_NOT_FILE,
                      vnode.ValidateOptions(fs::VnodeConnectionOptions::FromIoV1Flags(
                          fio::wire::OpenFlags::kNotDirectory)));
}

TEST(VnodeConnectionOptions, ValidateOptionsForService) {
  class TestConnector : public DummyVnode {
   public:
    fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kConnector; }
  };

  TestConnector vnode;
  EXPECT_RESULT_ERROR(ZX_ERR_NOT_DIR,
                      vnode.ValidateOptions(fs::VnodeConnectionOptions::FromIoV1Flags(
                          fio::wire::OpenFlags::kDirectory)));
  EXPECT_RESULT_OK(vnode.ValidateOptions(
      fs::VnodeConnectionOptions::FromIoV1Flags(fio::wire::OpenFlags::kNotDirectory)));
}

TEST(VnodeConnectionOptions, ValidateOptionsForFile) {
  class TestFile : public DummyVnode {
   public:
    fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kFile; }
  };

  TestFile vnode;
  EXPECT_RESULT_ERROR(ZX_ERR_NOT_DIR,
                      vnode.ValidateOptions(fs::VnodeConnectionOptions::FromIoV1Flags(
                          fio::wire::OpenFlags::kDirectory)));
  EXPECT_RESULT_OK(vnode.ValidateOptions(
      fs::VnodeConnectionOptions::FromIoV1Flags(fio::wire::OpenFlags::kNotDirectory)));
}

TEST(VnodeProtocolSet, Union) {
  auto file = fs::VnodeProtocol::kFile;
  auto directory = fs::VnodeProtocol::kDirectory;

  auto combined = file | directory;
  static_assert(std::is_same_v<decltype(combined), fs::VnodeProtocolSet>);

  // Note: using |EXPECT_FALSE| and explicit |operator==()| here and elsewhere as directly using
  // |EXPECT_EQ| does not seem to pick up our |operator==()| definition and fails to compile.
  EXPECT_FALSE(combined == fs::VnodeProtocol::kFile);
  EXPECT_FALSE(combined == fs::VnodeProtocol::kDirectory);

  EXPECT_TRUE((combined & file).any());
  EXPECT_TRUE((combined & directory).any());
  EXPECT_FALSE((combined & fs::VnodeProtocol::kConnector).any());
}

TEST(VnodeProtocolSet, Intersection) {
  auto file_plus_directory = fs::VnodeProtocol::kFile | fs::VnodeProtocol::kDirectory;
  auto directory_plus_connector = fs::VnodeProtocol::kDirectory | fs::VnodeProtocol::kConnector;

  auto intersection = file_plus_directory & directory_plus_connector;
  static_assert(std::is_same_v<decltype(intersection), fs::VnodeProtocolSet>);

  EXPECT_TRUE(intersection == fs::VnodeProtocol::kDirectory);

  EXPECT_TRUE((intersection & fs::VnodeProtocol::kDirectory).any());
  EXPECT_FALSE((intersection & fs::VnodeProtocol::kConnector).any());
  EXPECT_FALSE((intersection & fs::VnodeProtocol::kFile).any());
}

TEST(VnodeProtocolSet, Difference) {
  auto difference =
      (fs::VnodeProtocol::kFile | fs::VnodeProtocol::kDirectory | fs::VnodeProtocol::kConnector)
          .Except(fs::VnodeProtocol::kConnector);
  EXPECT_TRUE(difference.any());
  EXPECT_TRUE(difference == (fs::VnodeProtocol::kFile | fs::VnodeProtocol::kDirectory));
}

TEST(VnodeProtocolSet, ConvertToSingleProtocol) {
  fs::VnodeProtocolSet file(fs::VnodeProtocol::kFile);
  ASSERT_TRUE(file.which());
  ASSERT_EQ(file.which().value(), fs::VnodeProtocol::kFile);

  // The |kConnector| case is significant, because it's the first (zero-th) member in the bit-field.
  // Refer to the internal implementation of |fs::VnodeProtocol| and |fs::VnodeProtocolSet|.
  fs::VnodeProtocolSet connector(fs::VnodeProtocol::kConnector);
  ASSERT_TRUE(connector.which());
  ASSERT_EQ(connector.which().value(), fs::VnodeProtocol::kConnector);

  auto file_plus_directory = fs::VnodeProtocol::kFile | fs::VnodeProtocol::kDirectory;
  ASSERT_FALSE(file_plus_directory.which());
}

TEST(VnodeProtocolSet, All) {
  auto all = fs::VnodeProtocolSet::All();
  ASSERT_TRUE(all.any());

  EXPECT_TRUE((all & fs::VnodeProtocol::kConnector) == fs::VnodeProtocol::kConnector);
  EXPECT_TRUE((all & fs::VnodeProtocol::kDirectory) == fs::VnodeProtocol::kDirectory);
  EXPECT_TRUE((all & fs::VnodeProtocol::kFile) == fs::VnodeProtocol::kFile);
}

TEST(VnodeProtocolSet, Empty) {
  auto empty = fs::VnodeProtocolSet::Empty();
  ASSERT_FALSE(empty.any());

  auto empty_then_intersection = empty & fs::VnodeProtocol::kDirectory;
  ASSERT_FALSE(empty_then_intersection.any());
}

}  // namespace
