// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug.h"

#include "mac_frame.h"

#include <cstring>
#include <sstream>
#include <string>

namespace wlan {
namespace debug {

std::string Describe(const SequenceControl& sc) {
    std::ostringstream output;
    output << "frag: " << std::dec << std::showbase << sc.frag() << " ";
    output << "seq: " << std::dec << std::showbase << sc.seq() << " ";
    return output.str();
}

std::string Describe(const FrameHeader& hdr) {
    // TODO(porce): Support A-MSDU case
    std::ostringstream output;
    output << "fc: " << std::hex << std::showbase << hdr.fc.val() << " ";
    output << "dur: " << std::dec << std::showbase << hdr.duration << " ";

    // IEEE Std 802.11-2016, Table 9-26
    uint8_t ds = (hdr.fc.to_ds() << 1) + hdr.fc.from_ds();
    switch (ds) {
    case 0x0:
        output << "ra(da): " << hdr.addr1.ToString() << " ta(sa): " << hdr.addr2.ToString()
               << " bssid: " << hdr.addr3.ToString() << " ";
        break;
    case 0x1:
        output << "ra(da): " << hdr.addr1.ToString() << " ta(bssid): " << hdr.addr2.ToString()
               << " sa: " << hdr.addr3.ToString() << " ";
        break;
    case 0x2:
        output << "ra(bssid): " << hdr.addr1.ToString() << " ta(sa): " << hdr.addr2.ToString()
               << " da: " << hdr.addr3.ToString() << " ";
        break;
    case 0x3:
        output << "ra: " << hdr.addr1.ToString() << " ta: " << hdr.addr2.ToString()
               << " da: " << hdr.addr3.ToString() << " ";
        break;
    default:
        break;
    }

    output << Describe(hdr.sc);
    return output.str();
}

std::string Describe(const DataFrameHeader& hdr) {
    std::string output = Describe(*reinterpret_cast<const FrameHeader*>(&hdr));

    if (hdr.HasAddr4()) { output += " addr4: " + hdr.addr4()->ToString(); }
    return output;
}

}  // namespace debug
}  // namespace wlan
