// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "l2cap.h"

#include "garnet/drivers/bluetooth/lib/hci/transport.h"

#include "channel_manager.h"

namespace btlib {
namespace l2cap {
namespace {

class Impl final : public L2CAP {
 public:
  Impl(fxl::RefPtr<hci::Transport> hci,
       fxl::RefPtr<fxl::TaskRunner> l2cap_runner)
      : L2CAP(), hci_(hci), l2cap_runner_(l2cap_runner) {
    FXL_DCHECK(hci_);
    FXL_DCHECK(l2cap_runner_);
  }

  void Initialize() override {
    PostMessage([this] {
      // This can only run once during initialization.
      FXL_DCHECK(!chanmgr_);
      chanmgr_ = std::make_unique<ChannelManager>(hci_, l2cap_runner_);
    });
  }

  void ShutDown() override {
    PostMessage([this] { chanmgr_ = nullptr; });
  }

  void RegisterLE(hci::ConnectionHandle handle,
                  hci::Connection::Role role,
                  const LEConnectionParameterUpdateCallback& callback,
                  fxl::RefPtr<fxl::TaskRunner> task_runner) override {
    PostMessage([this, handle, role, callback, task_runner] {
      if (chanmgr_) {
        chanmgr_->RegisterLE(handle, role, callback, task_runner);
      }
    });
  }

  void Unregister(hci::ConnectionHandle handle) override {
    PostMessage([this, handle] {
      if (chanmgr_) {
        chanmgr_->Unregister(handle);
      }
    });
  }

  void OpenFixedChannel(hci::ConnectionHandle handle,
                        ChannelId id,
                        ChannelCallback callback,
                        fxl::RefPtr<fxl::TaskRunner> callback_runner) override {
    PostMessage([this, handle, id, cb = std::move(callback),
                 callback_runner]() mutable {
      if (!chanmgr_)
        return;

      auto chan = chanmgr_->OpenFixedChannel(handle, id);
      callback_runner->PostTask([chan, cb = std::move(cb)] { cb(chan); });
    });
  }

 private:
  void PostMessage(std::function<void()> func) {
    // Capture a reference so the object remains alive.
    l2cap_runner_->PostTask([refptr = fbl::RefPtr<L2CAP>(this),
                             func = std::move(func)] { func(); });
  }

  fxl::RefPtr<hci::Transport> hci_;
  fxl::RefPtr<fxl::TaskRunner> l2cap_runner_;

  // This must be only accessed on |l2cap_runner_|.
  std::unique_ptr<ChannelManager> chanmgr_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Impl);
};

}  // namespace

// static
fbl::RefPtr<L2CAP> L2CAP::Create(fxl::RefPtr<hci::Transport> hci,
                                 fxl::RefPtr<fxl::TaskRunner> l2cap_runner) {
  FXL_DCHECK(hci);
  FXL_DCHECK(l2cap_runner);

  return AdoptRef(new Impl(hci, l2cap_runner));
}

}  // namespace l2cap
}  // namespace btlib
