// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/buffer_writer.h>
#include <wlan/common/macaddr.h>
#include <wlan/common/mac_frame.h>
#include <wlan/mlme/sequence.h>

namespace wlan {

// Note that the current implementation might not fill out all the fields.
// Please examine the code in *.cpp before using it for your purpose.
class MacHeaderWriter {
 public:
     explicit MacHeaderWriter(const common::MacAddr& src_addr, Sequence* seq)
       : src_addr_(src_addr), seq_(seq) { }

     void WriteMeshMgmtHeader(BufferWriter* writer,
                              ManagementSubtype subtype,
                              const common::MacAddr& dst_addr) const;

 private:
    common::MacAddr src_addr_;
    Sequence* seq_;
};

} // namespace wlan
