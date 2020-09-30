// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_SYSINFO_SYSINFO_H_
#define SRC_BRINGUP_BIN_SYSINFO_SYSINFO_H_

#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

namespace sysinfo {
class SysInfo : public ::llcpp::fuchsia::sysinfo::SysInfo::Interface {
 public:
  SysInfo() {}
  // fuchsia.sysinfo.SysInfo methods
  void GetHypervisorResource(GetHypervisorResourceCompleter::Sync& completer);
  void GetBoardName(GetBoardNameCompleter::Sync& completer);
  void GetBoardRevision(GetBoardRevisionCompleter::Sync& completer);
  void GetBootloaderVendor(GetBootloaderVendorCompleter::Sync& completer);
  void GetInterruptControllerInfo(GetInterruptControllerInfoCompleter::Sync& completer);

 private:
  zx_status_t GetHypervisorResource(zx::resource* hypervisor);
  zx_status_t GetBoardName(std::string* board_name);
  zx_status_t GetBoardRevision(uint32_t* board_revision);
  zx_status_t GetBootloaderVendor(std::string* bootloader_vendor);
  zx_status_t GetInterruptControllerInfo(::llcpp::fuchsia::sysinfo::InterruptControllerInfo* info);
  zx_status_t ConnectToPBus(llcpp::fuchsia::sysinfo::SysInfo::SyncClient* client);
};
}  // namespace sysinfo
#endif  // SRC_BRINGUP_BIN_SYSINFO_SYSINFO_H_
