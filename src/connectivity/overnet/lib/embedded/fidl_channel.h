// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fuchsia/overnet/protocol/cpp/fidl.h>
#include <map>
#include "src/connectivity/overnet/lib/embedded/zx_channel.h"

namespace overnet {
namespace internal {

class Decoder;
class Encoder;
class FidlChannel;

// IO implementation details for FidlChannel... exposed here so that it can be
// forward declared for Encoder/Decoder.
class FidlChannelIO final : public fbl::RefCounted<FidlChannelIO>,
                            public fuchsia::overnet::protocol::ZirconChannel {
 public:
  using ReplyFunc = fit::function<zx_status_t(
      fuchsia::overnet::protocol::ZirconChannelMessage)>;

  FidlChannelIO(FidlChannel* parent, ClosedPtr<ZxChannel> channel);
  ~FidlChannelIO();
  FidlChannelIO(const FidlChannelIO&) = delete;
  FidlChannelIO& operator=(const FidlChannelIO&) = delete;

  void Send(const fidl_type_t* type,
            fuchsia::overnet::protocol::ZirconChannelMessage message,
            ReplyFunc callback);
  void Send(const fidl_type_t* type,
            fuchsia::overnet::protocol::ZirconChannelMessage message);
  ClosedPtr<ZxChannel> TakeChannel();
  ZxChannel* channel() { return channel_.get(); }

  void Orphan() { parent_ = nullptr; }

  void Message(
      fuchsia::overnet::protocol::ZirconChannelMessage message) override;

 private:
  FidlChannel* parent_;
  zx_txid_t next_txid_ = 1;
  std::map<zx_txid_t, ReplyFunc> pending_;
  ClosedPtr<ZxChannel> channel_;
};

// An interface for sending FIDL messages to a remote implementation.
class FidlChannel {
  friend class Encoder;
  friend class Decoder;
  friend class FidlChannelIO;

 public:
  explicit FidlChannel(ClosedPtr<ZxChannel> channel);
  virtual ~FidlChannel();

  ClosedPtr<ZxChannel> TakeChannel_();

 protected:
  fbl::RefPtr<FidlChannelIO> io_;

 private:
  // A new message has arrived.
  //
  // The memory backing the message will remain valid until this method returns,
  // at which point the memory might or might not be deallocated.
  virtual zx_status_t Dispatch_(
      fuchsia::overnet::protocol::ZirconChannelMessage message) = 0;
};

}  // namespace internal
}  // namespace overnet
