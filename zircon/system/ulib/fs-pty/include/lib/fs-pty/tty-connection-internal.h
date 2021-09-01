// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FS_PTY_TTY_CONNECTION_INTERNAL_H_
#define LIB_FS_PTY_TTY_CONNECTION_INTERNAL_H_

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>

namespace fs_pty::internal {

// We would like to construct a |NullPtyDevice| with some arbitrary arguments.
// This class exists so that we don't need to templatize all of the implementation,
// just the ctor.  The extra argument to the ctor in |NullPtyDevice| is discarded.
class NullPtyDeviceImpl : public fidl::WireServer<fuchsia_hardware_pty::Device> {
 public:
  NullPtyDeviceImpl() = default;
  ~NullPtyDeviceImpl() override = default;

  // fuchsia.hardware.pty.Device methods
  void OpenClient(OpenClientRequestView request, OpenClientCompleter::Sync& completer) final;
  void ClrSetFeature(ClrSetFeatureRequestView request,
                     ClrSetFeatureCompleter::Sync& completer) final;
  void GetWindowSize(GetWindowSizeRequestView request,
                     GetWindowSizeCompleter::Sync& completer) final;
  void MakeActive(MakeActiveRequestView request, MakeActiveCompleter::Sync& completer) final;
  void ReadEvents(ReadEventsRequestView request, ReadEventsCompleter::Sync& completer) final;
  void SetWindowSize(SetWindowSizeRequestView request,
                     SetWindowSizeCompleter::Sync& completer) final;

  // fuchsia.io.File methods (which were composed by fuchsia.hardware.pty.Device)
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) final;
  void ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) final;

  void Write(WriteRequestView request, WriteCompleter::Sync& completer) final;
  void WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) final;

  void Seek(SeekRequestView request, SeekCompleter::Sync& completer) final;
  void Truncate(TruncateRequestView request, TruncateCompleter::Sync& completer) final;
  void GetFlags(GetFlagsRequestView request, GetFlagsCompleter::Sync& completer) final;
  void SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) final;
  void GetBuffer(GetBufferRequestView request, GetBufferCompleter::Sync& completer) final;

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) final;
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) final;
  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) final;
  void Sync(SyncRequestView request, SyncCompleter::Sync& completer) final;
  void GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) final;
  void SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) final;
};

template <typename Console>
class NullPtyDevice : public NullPtyDeviceImpl {
 public:
  NullPtyDevice(Console console) : NullPtyDeviceImpl() {}
  ~NullPtyDevice() override = default;
};

}  // namespace fs_pty::internal

#endif  // LIB_FS_PTY_TTY_CONNECTION_INTERNAL_H_
