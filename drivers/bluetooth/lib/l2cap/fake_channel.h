// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fragmenter.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth {
namespace l2cap {
namespace testing {

// FakeChannel is a simple pass-through Channel implementation that is intended
// for L2CAP service level unit tests where data is transmitted over a L2CAP
// channel.
class FakeChannel : public Channel {
 public:
  FakeChannel(ChannelId id, hci::ConnectionHandle handle);
  ~FakeChannel() override = default;

  // Routes the given data over to the rx handler as if it was received from the
  // controller.
  void Receive(const common::ByteBuffer& data);

  // Sets a delegate to notify when a frame was sent over the channel.
  using SendCallback =
      std::function<void(std::unique_ptr<const common::ByteBuffer>)>;
  void SetSendCallback(const SendCallback& callback,
                       fxl::RefPtr<fxl::TaskRunner> task_runner);

  // Emulates channel closure.
  void Close();

  fxl::WeakPtr<FakeChannel> AsWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 protected:
  // Channel overrides:
  bool Send(std::unique_ptr<const common::ByteBuffer> sdu) override;
  void SetRxHandler(const RxCallback& rx_cb,
                    fxl::RefPtr<fxl::TaskRunner> rx_task_runner) override;

 private:
  Fragmenter fragmenter_;
  RxCallback rx_cb_;
  fxl::RefPtr<fxl::TaskRunner> rx_task_runner_;
  SendCallback send_cb_;
  fxl::RefPtr<fxl::TaskRunner> send_task_runner_;

  fxl::WeakPtrFactory<FakeChannel> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeChannel);
};

}  // namespace testing
}  // namespace l2cap
}  // namespace bluetooth
