// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_VFS_TYPES_H_
#define FS_VFS_TYPES_H_

#include <lib/fdio/vfs.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/types.h>

#include <cstdint>
#include <cstring>
#include <utility>

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

}  // namespace fs

#endif  // FS_VFS_TYPES_H_
