// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_BUILTIN_DEVNODES_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_BUILTIN_DEVNODES_H_

#include <lib/async/dispatcher.h>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

constexpr char kNullDevName[] = "null";
constexpr char kZeroDevName[] = "zero";

class BuiltinDevVnode : public fs::Vnode, public fidl::WireServer<fuchsia_io::Directory> {
 public:
  explicit BuiltinDevVnode(bool null) : null_(null) {}

  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) override;

  zx_status_t Write(const void* data, size_t len, size_t off, size_t* out_actual) override;

  zx_status_t GetAttributes(fs::VnodeAttributes* a) override;

  fs::VnodeProtocol Negotiate(fs::VnodeProtocolSet protocols) const override;
  fs::VnodeProtocolSet GetProtocols() const override;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) override;
  // We need to provide explicit stubs for these vnode methods because they share the same name as
  // fidl::WireServer<fuchsia_io::Directory> methods.
  void Sync(SyncCallback closure) override {}
  zx_status_t QueryFilesystem(fuchsia_io::wire::FilesystemInfo* out) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Link(std::string_view name, fbl::RefPtr<Vnode> target) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Unlink(std::string_view name, bool must_be_dir) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Rename(fbl::RefPtr<Vnode> newdir, std::string_view oldname, std::string_view newname,
                     bool src_must_be_dir, bool dst_must_be_dir) override {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void HandleFsSpecificMessage(fidl::IncomingMessage& msg, fidl::Transaction* txn) override;

  // fuchsia.io.Node functionality is handled by the vfs, so we just close the connection.
  void GetAttributes(GetAttributesRequestView request,
                     GetAttributesCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void Describe2(Describe2RequestView request, Describe2Completer::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void Sync(SyncRequestView request, SyncCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void CloseDeprecated(CloseDeprecatedRequestView request,
                       CloseDeprecatedCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void SyncDeprecated(SyncDeprecatedRequestView request,
                      SyncDeprecatedCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void GetFlags(GetFlagsRequestView request, GetFlagsCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void QueryFilesystem(QueryFilesystemRequestView request,
                       QueryFilesystemCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void AdvisoryLock(AdvisoryLockRequestView request,
                    AdvisoryLockCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void AddInotifyFilter(AddInotifyFilterRequestView request,
                        AddInotifyFilterCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void Unlink(UnlinkRequestView request, UnlinkCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void Rename(RenameRequestView request, RenameCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  // fuchsia.io.Directory calls.
  // We implement these because v1 components that call open("/dev/null") ends up being an open(".")
  // call over a device connection.
  void Open(OpenRequestView request, OpenCompleter::Sync& completer) override;
  void ReadDirents(ReadDirentsRequestView request, ReadDirentsCompleter::Sync& completer) override;
  void Rewind(RewindRequestView request, RewindCompleter::Sync& completer) override;
  void GetToken(GetTokenRequestView request, GetTokenCompleter::Sync& completer) override;
  void Link(LinkRequestView request, LinkCompleter::Sync& completer) override;
  void Watch(WatchRequestView request, WatchCompleter::Sync& completer) override;

 private:
  bool null_;
};

class BuiltinDevices {
 public:
  static BuiltinDevices* Get(async_dispatcher_t* dispatcher);
  // Clear the existing BuiltinDevices, and free it. Only for use in tests.
  static void Reset();

  // Called when /dev/null or /dev/zero are opened
  zx_status_t HandleOpen(fuchsia_io::OpenFlags flags, fidl::ServerEnd<fuchsia_io::Node> request,
                         std::string_view name);

 private:
  explicit BuiltinDevices(async_dispatcher_t* dispatcher) : vfs_(dispatcher) {}

  fbl::RefPtr<fs::Vnode> null_dev_ = fbl::MakeRefCounted<BuiltinDevVnode>(true);
  fbl::RefPtr<fs::Vnode> zero_dev_ = fbl::MakeRefCounted<BuiltinDevVnode>(false);
  fs::ManagedVfs vfs_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_BUILTIN_DEVNODES_H_
