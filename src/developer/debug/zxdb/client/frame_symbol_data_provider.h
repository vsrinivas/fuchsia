// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FRAME_SYMBOL_DATA_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FRAME_SYMBOL_DATA_PROVIDER_H_

#include "src/developer/debug/shared/register_id.h"
#include "src/developer/debug/zxdb/client/process_symbol_data_provider.h"

namespace zxdb {

class Frame;

// Implementation of SymbolDataProvider that links it to a frame. On top of the process' general
// memory read/write, this adds stack information and the instruction pointer.
class FrameSymbolDataProvider : public ProcessSymbolDataProvider {
 public:
  // SymbolDataProvider implementation:
  fxl::RefPtr<SymbolDataProvider> GetEntryDataProvider() const override;
  std::optional<containers::array_view<uint8_t>> GetRegister(debug::RegisterID id) override;
  void GetRegisterAsync(debug::RegisterID id, GetRegisterCallback callback) override;
  void WriteRegister(debug::RegisterID id, std::vector<uint8_t> data, WriteCallback cb) override;
  std::optional<uint64_t> GetFrameBase() override;
  void GetFrameBaseAsync(GetFrameBaseCallback callback) override;
  uint64_t GetCanonicalFrameAddress() const override;

 private:
  FRIEND_MAKE_REF_COUNTED(FrameSymbolDataProvider);
  FRIEND_REF_COUNTED_THREAD_SAFE(FrameSymbolDataProvider);

  explicit FrameSymbolDataProvider(fxl::WeakPtr<Frame> frame);
  ~FrameSymbolDataProvider() override;

  // The associated frame, possibly null if the data provider has outlived the frame.
  fxl::WeakPtr<Frame> frame_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FRAME_SYMBOL_DATA_PROVIDER_H_
