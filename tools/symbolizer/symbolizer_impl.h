// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_SYMBOLIZER_IMPL_H_
#define TOOLS_SYMBOLIZER_SYMBOLIZER_IMPL_H_

#include <iostream>
#include <string_view>
#include <unordered_map>

#include "src/developer/debug/ipc/records.h"
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

  // Mapping from module_id (available in the log) to module info.
  //
  // module_id is usually a sequence from 0 used to associate "mmap" commands with "module"
  // commands. It's different from build_id.
  std::unordered_map<int, debug_ipc::Module> modules_;

  // Since zxdb doesn't track the size of modules, we keep our own memory mapping here.
  struct AuxModuleInfo {
    uint64_t size;
    int id;
  };

  // Mapping from base address of each module to auxiliary module info.
  std::map<uint64_t, AuxModuleInfo> aux_modules_info_;
};

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_SYMBOLIZER_IMPL_H_
