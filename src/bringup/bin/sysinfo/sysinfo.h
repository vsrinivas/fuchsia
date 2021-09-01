// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_SYSINFO_SYSINFO_H_
#define SRC_BRINGUP_BIN_SYSINFO_SYSINFO_H_

#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

namespace sysinfo {
class SysInfo : public fidl::WireServer<fuchsia_sysinfo::SysInfo> {
 public:
  SysInfo() {}
  // fuchsia.sysinfo.SysInfo methods
  void GetBoardName(GetBoardNameRequestView request,
                    GetBoardNameCompleter::Sync& completer) override;
  void GetBoardRevision(GetBoardRevisionRequestView request,
                        GetBoardRevisionCompleter::Sync& completer) override;
  void GetBootloaderVendor(GetBootloaderVendorRequestView request,
                           GetBootloaderVendorCompleter::Sync& completer) override;
  void GetInterruptControllerInfo(GetInterruptControllerInfoRequestView request,
                                  GetInterruptControllerInfoCompleter::Sync& completer) override;

 private:
  zx_status_t GetBoardName(std::string* board_name);
  zx_status_t GetBoardRevision(uint32_t* board_revision);
  zx_status_t GetBootloaderVendor(std::string* bootloader_vendor);
  zx_status_t GetInterruptControllerInfo(fuchsia_sysinfo::wire::InterruptControllerInfo* info);
  zx_status_t ConnectToPBus(fidl::WireSyncClient<fuchsia_sysinfo::SysInfo>* client);
};
}  // namespace sysinfo
#endif  // SRC_BRINGUP_BIN_SYSINFO_SYSINFO_H_
