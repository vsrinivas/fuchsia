// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FS_PTY_TTY_CONNECTION_INTERNAL_H_
#define LIB_FS_PTY_TTY_CONNECTION_INTERNAL_H_

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>

namespace fs_pty::internal {

// This is the monomorphic part of |NullPtyDevice|.
class NullPtyDeviceImpl : public fidl::WireServer<fuchsia_hardware_pty::Device> {
 public:
  NullPtyDeviceImpl() = default;
  ~NullPtyDeviceImpl() override = default;

  // fuchsia.hardware.pty.Device methods
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

  // fuchsia.io.File methods (which were composed by fuchsia.hardware.pty.Device)
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) final;

  void Write(WriteRequestView request, WriteCompleter::Sync& completer) final;

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) final;
  void DescribeDeprecated(DescribeDeprecatedCompleter::Sync& completer) final;
  void GetAttr(GetAttrCompleter::Sync& completer) final;
  void SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) final;
  void GetFlags(GetFlagsCompleter::Sync& completer) final;
  void SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) final;
  void QueryFilesystem(QueryFilesystemCompleter::Sync& completer) final;
};

template <typename ConsoleOps, typename ConsoleState>
class NullPtyDevice : public NullPtyDeviceImpl {
 public:
  explicit NullPtyDevice(ConsoleState console) : NullPtyDeviceImpl(), console_(console) {}
  ~NullPtyDevice() override = default;

  // fuchsia.hardware.pty.Device methods
  void Describe2(Describe2Completer::Sync& completer) final {
    zx::eventpair event;
    if (zx_status_t status = ConsoleOps::GetEvent(console_, &event); status != ZX_OK) {
      completer.Close(status);
    } else {
      fidl::Arena alloc;
      completer.Reply(fuchsia_hardware_pty::wire::DeviceDescribe2Response::Builder(alloc)
                          .event(std::move(event))
                          .Build());
    }
  }

 private:
  ConsoleState console_;
};

}  // namespace fs_pty::internal

#endif  // LIB_FS_PTY_TTY_CONNECTION_INTERNAL_H_
