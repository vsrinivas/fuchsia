// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_PTYSVC_PTY_SERVER_DEVICE_H_
#define SRC_BRINGUP_BIN_PTYSVC_PTY_SERVER_DEVICE_H_

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <lib/zx/channel.h>

#include <fbl/ref_ptr.h>

#include "pty-server.h"

class PtyServerDevice : public fidl::WireServer<fuchsia_hardware_pty::Device> {
 public:
  explicit PtyServerDevice(fbl::RefPtr<PtyServer> server) : server_(std::move(server)) {}

  ~PtyServerDevice() override = default;

  // fuchsia.hardware.pty.Device methods
  void Describe2(Describe2Completer::Sync& completer) final;
  void OpenClient(OpenClientRequestView request, OpenClientCompleter::Sync& completer) final;
  void ClrSetFeature(ClrSetFeatureRequestView request,
                     ClrSetFeatureCompleter::Sync& completer) final;
  void GetWindowSize(GetWindowSizeCompleter::Sync& completer) final;
  void MakeActive(MakeActiveRequestView request, MakeActiveCompleter::Sync& completer) final;
  void ReadEvents(ReadEventsCompleter::Sync& completer) final;
  void SetWindowSize(SetWindowSizeRequestView request,
                     SetWindowSizeCompleter::Sync& completer) final;

  // fuchsia.unknown.Cloneable.
  void Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) final;

  // fuchsia.unknown.Closeable.
  void Close(CloseCompleter::Sync& completer) final;

  // fuchsia.unknown.Queryable.
  void Query(QueryCompleter::Sync& completer) final;

  // fuchsia.io.File methods
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) final;
  void ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) final;

  void Write(WriteRequestView request, WriteCompleter::Sync& completer) final;
  void WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) final;

  void Seek(SeekRequestView request, SeekCompleter::Sync& completer) final;
  void Resize(ResizeRequestView request, ResizeCompleter::Sync& completer) final;
  void GetBackingMemory(GetBackingMemoryRequestView request,
                        GetBackingMemoryCompleter::Sync& completer) final;

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) final;
  void DescribeDeprecated(DescribeDeprecatedCompleter::Sync& completer) final;
  void GetAttr(GetAttrCompleter::Sync& completer) final;
  void SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) final;
  void GetFlags(GetFlagsCompleter::Sync& completer) final;
  void SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) final;
  void QueryFilesystem(QueryFilesystemCompleter::Sync& completer) final;

 private:
  fbl::RefPtr<PtyServer> server_;
};

#endif  // SRC_BRINGUP_BIN_PTYSVC_PTY_SERVER_DEVICE_H_
