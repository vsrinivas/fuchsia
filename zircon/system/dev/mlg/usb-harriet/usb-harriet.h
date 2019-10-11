// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <usb/usb.h>

namespace usb_harriet {

class Harriet;
using HarrietBase = ddk::Device<Harriet, ddk::UnbindableNew>;

class Harriet : public HarrietBase, public ddk::EmptyProtocol<ZX_PROTOCOL_MLG> {
 public:
  Harriet(zx_device_t* parent, const usb::UsbDevice& usb) : HarrietBase(parent), usb_(usb) {}

  // Spawns device node based on parent node.
  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  zx_status_t Bind();

  usb::UsbDevice usb_;
};

}  // namespace usb_harriet
