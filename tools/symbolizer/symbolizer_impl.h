// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_SYMBOLIZER_IMPL_H_
#define TOOLS_SYMBOLIZER_SYMBOLIZER_IMPL_H_

#include <iostream>
#include <string_view>
#include <unordered_map>

#include "src/developer/debug/shared/message_loop_poll.h"
#include "src/developer/debug/zxdb/client/download_observer.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/system_observer.h"
#include "tools/symbolizer/command_line_options.h"
#include "tools/symbolizer/printer.h"
#include "tools/symbolizer/symbolizer.h"

namespace symbolizer {

// This is the core logic of the symbolizer. We provide a MockSymbolizer and a SymbolizerImpl for
// better testing.
class SymbolizerImpl : public Symbolizer,
                       public zxdb::DownloadObserver,
                       public zxdb::SystemObserver {
 public:
  SymbolizerImpl(Printer* printer, const CommandLineOptions& options);
  ~SymbolizerImpl() override;

  // |Symbolizer| implementation.
  void Reset() override;
  void Module(uint64_t id, std::string_view name, std::string_view build_id) override;
  void MMap(uint64_t address, uint64_t size, uint64_t module_id, uint64_t module_offset) override;
  void Backtrace(int frame_id, uint64_t address, AddressType type,
                 std::string_view message) override;

  // |DownloadObserver| implementation.
  void OnDownloadsStarted() override;
  void OnDownloadsStopped(size_t num_succeeded, size_t num_failed) override;

  // |SystemObserver| implementation.
  void DidCreateSymbolServer(zxdb::SymbolServer* server) override;
  void OnSymbolServerStatusChanged(zxdb::SymbolServer* server) override;

 private:
  // Ensures a process is created on target_. Should be called before each Bactrace().
  void InitProcess();

  // Non-owning.
  Printer* printer_;

  // The main message loop.
  debug_ipc::MessageLoopPoll loop_;

  // The entry for interacting with zxdb.
  zxdb::Session session_;

  // Owned by session_. Holds the process we're working on.
  zxdb::Target* target_;

  // Whether there are symbol servers and we're waiting for authentication.
  bool waiting_auth_ = false;

  // Whether there are symbol downloads in progress.
  bool is_downloading_ = false;

  struct ModuleInfo {
    std::string name;
    std::string build_id;
    uint64_t base = 0;  // Load address of the module.
    uint64_t size = 0;  // Range of the module.

    // Zircon on x64 has a negative base address, i.e. the module offset is larger than the load
    // address. Since zxdb doesn't support that, we load the module at 0 and modify the pc for all
    // frames.
    //
    // At least one of the base and the negative_base must be zero.
    uint64_t negative_base = 0;
    bool printed = false;  // Whether we've printed the module info.
  };

  // Mapping from module_id (available in the log) to module info.
  //
  // module_id is usually a sequence from 0 used to associate "mmap" commands with "module"
  // commands. It's different from build_id.
  std::unordered_map<int, ModuleInfo> modules_;

  // Mapping from base address of each module to the module_id.
  // Useful when doing binary search for the module from an address.
  std::map<uint64_t, int> address_to_module_id_;
};

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_SYMBOLIZER_IMPL_H_
