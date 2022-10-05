// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/config/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/component/cpp/testing/internal/errors.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <memory>

#include "zircon/status.h"

namespace component_testing {

namespace {

constexpr char kSvcDirectoryPath[] = "/svc";

#define ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(MethodName, Type, FidlType) \
  ConfigValue ConfigValue::MethodName(Type value) {                                  \
    fuchsia::component::config::ValueSpec spec;                                      \
    spec.set_value(fuchsia::component::config::Value::WithSingle(                    \
        fuchsia::component::config::SingleValue::FidlType(std::move(value))));       \
    return ConfigValue(std::move(spec));                                             \
  }

#define ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_CTOR_DEF(Type, FidlType)  \
  ConfigValue::ConfigValue(Type value) {                                       \
    spec.set_value(fuchsia::component::config::Value::WithSingle(              \
        fuchsia::component::config::SingleValue::FidlType(std::move(value)))); \
  }

#define ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(Type, FidlType)  \
  ConfigValue::ConfigValue(Type value) {                                       \
    spec.set_value(fuchsia::component::config::Value::WithVector(              \
        fuchsia::component::config::VectorValue::FidlType(std::move(value)))); \
  }

// Checks that path doesn't contain leading nor trailing slashes.
bool IsValidPath(std::string_view path) {
  return !path.empty() && path.front() != '/' && path.back() != '/';
}
}  // namespace

LocalComponent::~LocalComponent() = default;

LocalComponentHandles::LocalComponentHandles(fdio_ns_t* ns, sys::OutgoingDirectory outgoing_dir)
    : namespace_(ns), outgoing_dir_(std::move(outgoing_dir)) {}

LocalComponentHandles::~LocalComponentHandles() {
  if (on_destruct_) {
    on_destruct_();
  }
  ZX_ASSERT(fdio_ns_destroy(namespace_) == ZX_OK);
}

LocalComponentHandles::LocalComponentHandles(LocalComponentHandles&& other) noexcept
    : namespace_(other.namespace_), outgoing_dir_(std::move(other.outgoing_dir_)) {
  other.namespace_ = nullptr;
}

LocalComponentHandles& LocalComponentHandles::operator=(LocalComponentHandles&& other) noexcept {
  namespace_ = other.namespace_;
  outgoing_dir_ = std::move(other.outgoing_dir_);
  other.namespace_ = nullptr;
  return *this;
}

fdio_ns_t* LocalComponentHandles::ns() { return namespace_; }

sys::OutgoingDirectory* LocalComponentHandles::outgoing() { return &outgoing_dir_; }

sys::ServiceDirectory LocalComponentHandles::svc() {
  zx::channel local;
  zx::channel remote;
  ZX_COMPONENT_ASSERT_STATUS_OK("zx::channel/create", zx::channel::create(0, &local, &remote));

  // TODO(https://fxbug.dev/101092): Replace this with fdio_ns_service_connect when
  // ServiceDirectory::Connect (via fdio_service_connect_at) no longer requests R/W.
  constexpr uint32_t kServiceFlags = static_cast<uint32_t>(fuchsia::io::OpenFlags::RIGHT_READABLE |
                                                           fuchsia::io::OpenFlags::RIGHT_WRITABLE);
  auto status = fdio_ns_open(namespace_, kSvcDirectoryPath, kServiceFlags, remote.release());
  ZX_ASSERT_MSG(status == ZX_OK,
                "fdio_ns_service_connect on LocalComponent's /svc directory failed: %s\nThis most"
                "often occurs when a component has no FIDL protocols routed to it.",
                zx_status_get_string(status));

  return sys::ServiceDirectory(std::move(local));
}

void LocalComponentHandles::Exit(zx_status_t return_code) {
  // Disable checks for premature LocalComponentHandles destruction:
  on_destruct_ = nullptr;
  if (on_exit_) {
    on_exit_(return_code);
  }
}

constexpr size_t kDefaultVmoSize = 4096;

DirectoryContents& DirectoryContents::AddFile(std::string_view path, BinaryContents contents) {
  ZX_ASSERT_MSG(IsValidPath(path), "[DirectoryContents/AddFile] Encountered invalid path: %s",
                path.data());

  zx::vmo vmo;
  ZX_COMPONENT_ASSERT_STATUS_OK("AddFile/zx_vmo_create", zx::vmo::create(kDefaultVmoSize, 0, &vmo));
  ZX_COMPONENT_ASSERT_STATUS_OK("AddFile/zx_vmo_write",
                                vmo.write(contents.buffer, contents.offset, contents.size));
  fuchsia::mem::Buffer out_buffer{.vmo = std::move(vmo), .size = contents.size};
  contents_.entries.emplace_back(fuchsia::component::test::DirectoryEntry{
      .file_path = std::string(path), .file_contents = std::move(out_buffer)});
  return *this;
}

DirectoryContents& DirectoryContents::AddFile(std::string_view path, std::string_view contents) {
  return AddFile(path,
                 BinaryContents{.buffer = contents.data(), .size = contents.size(), .offset = 0});
}

fuchsia::component::test::DirectoryContents DirectoryContents::TakeAsFidl() {
  return std::move(contents_);
}

ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_CTOR_DEF(std::string, WithString)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_CTOR_DEF(const char*, WithString)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(Bool, bool, WithBool_)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(Uint8, uint8_t, WithUint8)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(Uint16, uint16_t, WithUint16)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(Uint32, uint32_t, WithUint32)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(Uint64, uint64_t, WithUint64)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(Int8, int8_t, WithInt8)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(Int16, int16_t, WithInt16)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(Int32, int32_t, WithInt32)
ZX_SYS_COMPONENT_REPLACE_CONFIG_SINGLE_VALUE_DEF(Int64, int64_t, WithInt64)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<bool>, WithBoolVector)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<uint8_t>, WithUint8Vector)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<uint16_t>, WithUint16Vector)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<uint32_t>, WithUint32Vector)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<uint64_t>, WithUint64Vector)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<int8_t>, WithInt8Vector)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<int16_t>, WithInt16Vector)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<int32_t>, WithInt32Vector)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<int64_t>, WithInt64Vector)
ZX_SYS_COMPONENT_REPLACE_CONFIG_VECTOR_VALUE_CTOR_DEF(std::vector<std::string>, WithStringVector)

ConfigValue::ConfigValue(fuchsia::component::config::ValueSpec spec) : spec(std::move(spec)) {}
ConfigValue& ConfigValue::operator=(ConfigValue&& other) noexcept {
  spec = std::move(other.spec);
  return *this;
}
ConfigValue::ConfigValue(ConfigValue&& other) noexcept : spec(std::move(other.spec)) {}
fuchsia::component::config::ValueSpec ConfigValue::TakeAsFidl() { return std::move(spec); }

}  // namespace component_testing
