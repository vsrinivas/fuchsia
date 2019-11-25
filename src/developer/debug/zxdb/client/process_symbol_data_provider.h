// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_SYMBOL_DATA_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_SYMBOL_DATA_PROVIDER_H_

#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

namespace zxdb {

class Process;

// Implementation of SymbolDataProvider that links it to a process. It provides access to process
// memory but reports errors for all attempts to access frame-related information such as registers.
// For that, see FrameSymbolDataProvider.
class ProcessSymbolDataProvider : public SymbolDataProvider {
 public:
  // Called by the process when it's being destroyed. This will remove the back-pointer to the
  // process and all future requests for data will fail.
  //
  // This is necessary because this class is reference counted and may outlive the process due to
  // in-progress operations.
  virtual void Disown();

  // SymbolDataProvider overrides:
  debug_ipc::Arch GetArch() override;
  void GetMemoryAsync(uint64_t address, uint32_t size, GetMemoryCallback callback) override;
  void WriteMemory(uint64_t address, std::vector<uint8_t> data, WriteCallback cb) override;

 protected:
  FRIEND_MAKE_REF_COUNTED(ProcessSymbolDataProvider);
  FRIEND_REF_COUNTED_THREAD_SAFE(ProcessSymbolDataProvider);

  explicit ProcessSymbolDataProvider(Process* process);
  ~ProcessSymbolDataProvider() override;

 private:
  // The associated process, possibly null if it has been disowned.
  Process* process_;
  debug_ipc::Arch arch_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_SYMBOL_DATA_PROVIDER_H_
