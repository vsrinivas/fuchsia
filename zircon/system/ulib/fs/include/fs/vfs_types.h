// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_VFS_TYPES_H_
#define FS_VFS_TYPES_H_

#include <lib/fdio/vfs.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/types.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/handle.h>
#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>
#endif

#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <fbl/bitfield.h>

// The filesystem server exposes various FIDL protocols on top of the Vnode abstractions.
// In order to achieve the following objectives:
// - the FIDL protocol and the Vnode APIs can evolve independently from each other
// - the Vnode APIs can be tested in isolation without relying on FIDL
// - the Vnode APIs structures have recursive ownership semantics, simplifying passing around
// We explicitly define a set of filesystem types to be used by the Vnode interface, as opposed to
// blindly reusing the FIDL generated types. The names of these types all begin with "Vnode"
// to reduce confusion with their FIDL counterparts.
namespace fs {

union Rights {
  uint32_t raw_value = 0;
  fbl::BitFieldMember<uint32_t, 0, 1> read;
  fbl::BitFieldMember<uint32_t, 1, 1> write;
  fbl::BitFieldMember<uint32_t, 2, 1> admin;
  fbl::BitFieldMember<uint32_t, 3, 1> execute;

  constexpr Rights() : raw_value(0) {}

  // True if any right is present.
  bool any() const { return raw_value != 0; }

  Rights& operator|=(Rights other) {
    raw_value |= other.raw_value;
    return *this;
  }

  constexpr Rights& operator=(const Rights& other) {
    raw_value = other.raw_value;
    return *this;
  }

  // Returns true if the rights does not exceed those in |other|.
  bool StricterOrSameAs(Rights other) const { return (raw_value & ~(other.raw_value)) == 0; }

  // Convenience factory functions for commonly used option combinations.

  constexpr static Rights ReadOnly() {
    Rights rights;
    rights.read = true;
    return rights;
  }

  constexpr static Rights WriteOnly() {
    Rights rights;
    rights.write = true;
    return rights;
  }

  constexpr static Rights ReadWrite() {
    Rights rights;
    rights.read = true;
    rights.write = true;
    return rights;
  }

  constexpr static Rights ReadExec() {
    Rights rights;
    rights.read = true;
    rights.execute = true;
    return rights;
  }

  constexpr static Rights All() {
    Rights rights;
    rights.read = true;
    rights.write = true;
    rights.admin = true;
    rights.execute = true;
    return rights;
  }
};

// Options specified during opening and cloning.
struct VnodeConnectionOptions {
  // TODO(fxb/38160): Harmonize flags and rights to express both fuchsia.io v1 and v2 semantics.
  // For now, these map to the corresponding items in io.fidl. Refer to that file for
  // documentation.
  union Flags {
    uint32_t raw_value = 0;
    fbl::BitFieldMember<uint32_t, 0, 1> create;
    fbl::BitFieldMember<uint32_t, 1, 1> fail_if_exists;
    fbl::BitFieldMember<uint32_t, 2, 1> truncate;
    fbl::BitFieldMember<uint32_t, 3, 1> directory;
    fbl::BitFieldMember<uint32_t, 4, 1> not_directory;
    fbl::BitFieldMember<uint32_t, 5, 1> append;
    fbl::BitFieldMember<uint32_t, 6, 1> no_remote;
    fbl::BitFieldMember<uint32_t, 7, 1> node_reference;
    fbl::BitFieldMember<uint32_t, 8, 1> describe;
    fbl::BitFieldMember<uint32_t, 9, 1> posix;
    fbl::BitFieldMember<uint32_t, 10, 1> clone_same_rights;

    constexpr Flags() : raw_value(0) {}

    constexpr Flags& operator=(const Flags& other) {
      raw_value = other.raw_value;
      return *this;
    }

  } flags = {};

  Rights rights = {};

  constexpr VnodeConnectionOptions set_directory() {
    flags.directory = true;
    return *this;
  }

  constexpr VnodeConnectionOptions set_no_remote() {
    flags.no_remote = true;
    return *this;
  }

  constexpr VnodeConnectionOptions set_node_reference() {
    flags.node_reference = true;
    return *this;
  }

  constexpr VnodeConnectionOptions set_truncate() {
    flags.truncate = true;
    return *this;
  }

  constexpr VnodeConnectionOptions set_create() {
    flags.create = true;
    return *this;
  }

  // Convenience factory functions for commonly used option combinations.

  constexpr static VnodeConnectionOptions ReadOnly() {
    VnodeConnectionOptions options;
    options.rights = Rights::ReadOnly();
    return options;
  }

  constexpr static VnodeConnectionOptions WriteOnly() {
    VnodeConnectionOptions options;
    options.rights = Rights::WriteOnly();
    return options;
  }

  constexpr static VnodeConnectionOptions ReadWrite() {
    VnodeConnectionOptions options;
    options.rights = Rights::ReadWrite();
    return options;
  }

  constexpr static VnodeConnectionOptions ReadExec() {
    VnodeConnectionOptions options;
    options.rights = Rights::ReadExec();
    return options;
  }

#ifdef __Fuchsia__
  // Converts from fuchsia.io v1 flags to |VnodeConnectionOptions|.
  static VnodeConnectionOptions FromIoV1Flags(uint32_t fidl_flags);

  // Converts from |VnodeConnectionOptions| to fuchsia.io flags.
  uint32_t ToIoV1Flags() const;

  // Some flags (e.g. POSIX) only affect the interpretation of rights at the time of
  // Open/Clone, and should have no effects thereafter. Hence we filter them here.
  // TODO(fxb/33336): Some of these flag groups should be defined in io.fidl and use that as the
  // source of truth.
  static VnodeConnectionOptions FilterForNewConnection(VnodeConnectionOptions options);
#endif  // __Fuchsia__
};

// Objective information about a filesystem node, used to implement |Vnode::GetAttributes|.
struct VnodeAttributes {
  uint32_t mode = {};
  uint64_t inode = {};
  uint64_t content_size = {};
  uint64_t storage_size = {};
  uint64_t link_count = {};
  uint64_t creation_time = {};
  uint64_t modification_time = {};

  bool operator==(const VnodeAttributes& other) const {
    return mode == other.mode && inode == other.inode && content_size == other.content_size &&
           storage_size == other.storage_size && link_count == other.link_count &&
           creation_time == other.creation_time && modification_time == other.modification_time;
  }
};

// A request to update pieces of the |VnodeAttributes|. The fuchsia.io protocol only
// allows mutating the creation time and modification time.
// When a field is present, it indicates that the corresponding field should be updated.
class VnodeAttributesUpdate {
 public:
  VnodeAttributesUpdate& set_creation_time(std::optional<uint64_t> v) {
    creation_time_ = v;
    return *this;
  }

  VnodeAttributesUpdate& set_modification_time(std::optional<uint64_t> v) {
    modification_time_ = v;
    return *this;
  }

  bool any() const { return creation_time_.has_value() || modification_time_.has_value(); }

  bool has_creation_time() const { return creation_time_.has_value(); }

  // Moves out the creation time. Requires |creation_time_| to be present.
  // After this method returns, |creation_time_| is absent.
  uint64_t take_creation_time() {
    uint64_t v = creation_time_.value();
    creation_time_ = std::nullopt;
    return v;
  }

  bool has_modification_time() const { return modification_time_.has_value(); }

  // Moves out the modification time. Requires |modification_time_| to be present.
  // After this method returns, |modification_time_| is absent.
  uint64_t take_modification_time() {
    uint64_t v = modification_time_.value();
    modification_time_ = std::nullopt;
    return v;
  }

 private:
  std::optional<uint64_t> creation_time_ = {};
  std::optional<uint64_t> modification_time_ = {};
};

#ifdef __Fuchsia__

// Describe how the vnode connection should be handled, and provides auxiliary handles
// and information for the connection where applicable.
class VnodeRepresentation {
 public:
  struct Connector {};

  struct File {
    zx::event observer = {};
  };

  struct Directory {};

  struct Pipe {
    zx::socket socket = {};
  };

  struct Memory {
    zx::vmo vmo = {};
    uint64_t offset = {};
    uint64_t length = {};
  };

  struct Device {
    zx::eventpair event = {};
  };

  struct Tty {
    zx::eventpair event = {};
  };

  struct Socket {
    zx::socket socket = {};
  };

  VnodeRepresentation() = default;

  // Forwards the constructor arguments into the underlying |std::variant|.
  // This allows |VnodeRepresentation| to be constructed directly from one of the variants, e.g.
  //
  //     VnodeRepresentation repr = VnodeRepresentation::Socket{.socket = zx::socket(...)};
  //
  template <typename T>
  VnodeRepresentation(T&& v) : variants_(std::forward<T>(v)) {}

  // Applies the |visitor| function to the variant payload. It simply forwards the visitor into
  // the underlying |std::variant|. Returns the return value of |visitor|.
  // Refer to C++ documentation for |std::visit|.
  template <class Visitor>
  constexpr auto visit(Visitor&& visitor) -> decltype(visitor(std::declval<Connector>())) {
    return std::visit(std::forward<Visitor>(visitor), variants_);
  }

  Connector& connector() { return std::get<Connector>(variants_); }

  bool is_connector() const { return std::holds_alternative<Connector>(variants_); }

  File& file() { return std::get<File>(variants_); }

  bool is_file() const { return std::holds_alternative<File>(variants_); }

  Directory& directory() { return std::get<Directory>(variants_); }

  bool is_directory() const { return std::holds_alternative<Directory>(variants_); }

  Pipe& pipe() { return std::get<Pipe>(variants_); }

  bool is_pipe() const { return std::holds_alternative<Pipe>(variants_); }

  Memory& memory() { return std::get<Memory>(variants_); }

  bool is_memory() const { return std::holds_alternative<Memory>(variants_); }

  Device& device() { return std::get<Device>(variants_); }

  bool is_device() const { return std::holds_alternative<Device>(variants_); }

  Tty& tty() { return std::get<Tty>(variants_); }

  bool is_tty() const { return std::holds_alternative<Tty>(variants_); }

  Socket& socket() { return std::get<Socket>(variants_); }

  bool is_socket() const { return std::holds_alternative<Socket>(variants_); }

 private:
  using Variants =
      std::variant<std::monostate, Connector, File, Directory, Pipe, Memory, Device, Tty, Socket>;

  Variants variants_ = {};
};

#endif  // __Fuchsia__

}  // namespace fs

#endif  // FS_VFS_TYPES_H_
