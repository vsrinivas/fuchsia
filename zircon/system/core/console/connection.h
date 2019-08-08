// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>

#include <fbl/ref_ptr.h>
#include <fs/connection.h>

#include "console.h"

class Connection final : public ::llcpp::fuchsia::hardware::pty::Device::Interface,
                         public fs::Connection {
 public:
  Connection(fbl::RefPtr<Console> console, fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
             zx::channel channel, uint32_t flags)
      : fs::Connection(vfs, std::move(vnode), std::move(channel), flags),
        console_(std::move(console)) {}

  ~Connection() final = default;

  // From fs::Connection
  zx_status_t HandleFsSpecificMessage(fidl_msg_t* msg, fidl_txn_t* txn) final;

  // fuchsia.hardware.pty.Device methods
  void OpenClient(uint32_t id, zx::channel client, OpenClientCompleter::Sync completer) final;
  void ClrSetFeature(uint32_t clr, uint32_t set, ClrSetFeatureCompleter::Sync completer) final;
  void GetWindowSize(GetWindowSizeCompleter::Sync completer) final;
  void MakeActive(uint32_t client_pty_id, MakeActiveCompleter::Sync completer) final;
  void ReadEvents(ReadEventsCompleter::Sync completer) final;
  void SetWindowSize(::llcpp::fuchsia::hardware::pty::WindowSize size,
                     SetWindowSizeCompleter::Sync completer) final;

  // fuchsia.io.File methods
  void Read(uint64_t count, ReadCompleter::Sync completer) final;
  void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) final;

  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) final;
  void WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
               WriteAtCompleter::Sync completer) final;

  void Seek(int64_t offset, ::llcpp::fuchsia::io::SeekOrigin start,
            SeekCompleter::Sync completer) final;
  void Truncate(uint64_t length, TruncateCompleter::Sync completer) final;
  void GetFlags(GetFlagsCompleter::Sync completer) final;
  void SetFlags(uint32_t flags, SetFlagsCompleter::Sync completer) final;
  void GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) final;

  void Clone(uint32_t flags, zx::channel node, CloneCompleter::Sync completer) final;
  void Close(CloseCompleter::Sync completer) final;
  void Describe(DescribeCompleter::Sync completer) final;
  void Sync(SyncCompleter::Sync completer) final;
  void GetAttr(GetAttrCompleter::Sync completer) final;
  void SetAttr(uint32_t flags, ::llcpp::fuchsia::io::NodeAttributes attributes,
               SetAttrCompleter::Sync completer) final;
  void Ioctl(uint32_t opcode, uint64_t max_out, fidl::VectorView<zx::handle> handles,
             fidl::VectorView<uint8_t> in, IoctlCompleter::Sync completer) final;

 private:
  const fbl::RefPtr<Console> console_;
};
