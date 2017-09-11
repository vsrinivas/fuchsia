// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/l2cap/pdu.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace l2cap {

// A Fragmenter is used to construct L2CAP PDUs composed of fragments that can be sent over the HCI
// ACL data channel. This is intended for building PDUs that will be sent in the host-to-controller
// direction only.
//
// Each instance of Fragmenter is intended to operate on a single logical link.
//
// THREAD-SAFETY:
//
// This class is not thread-safe. External locking should be provided if an instance will be
// accessed on multiple threads.
class Fragmenter final {
 public:
  // |max_acl_payload_size| is the maximum number of bytes that should be allowed in a ACL data
  // packet, exluding the header. |connection_handle| represents the logical link that this
  // Fragmenter operates on.
  //
  // NOTE: |max_acl_payload_size| is required by the spec to be at least 27 (see Core Spec v5.0, Vol
  // 2, Part E, Section 5.4.2). We do not enforce this here as unit tests are allowed to pass a
  // smaller number.
  Fragmenter(hci::ConnectionHandle connection_handle,
             uint16_t max_acl_payload_size = hci::kMaxACLPayloadSize);

  void set_max_acl_payload_size(size_t value) { max_acl_payload_size_ = value; }

  // Constructs and returns a PDU to be sent over the L2CAP channel |channel_id|. |data| will be
  // treated as the Information payload of a B-frame, i.e. the PDU will contain:
  //
  //   [Basic L2CAP header][data]
  //
  // All other L2CAP frames are based on the B-frame and can be constructed using this method.
  //
  // If |flushable| is true, then this will build an automatically flushable L2CAP PDU.
  // Automatically flushable packets are sent over ACL-U logical links based on the setting of an
  // automatic flush timer. Only non-automatically flushable PDUs can be sent over LE-U links
  // (see Core Spec v5.0, Vol 2, Part E, Section 5.4.2).
  PDU BuildBasicFrame(ChannelId channel_id, const common::ByteBuffer& data, bool flushable = false);

 private:
  hci::ConnectionHandle connection_handle_;
  uint16_t max_acl_payload_size_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Fragmenter);
};

}  // namespace l2cap
}  // namespace bluetooth
