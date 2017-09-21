// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace common {

class ByteBuffer;

// Utility class for logging HCI traffic in the BTSnoop file format. See
// http://fte.com/webhelp/sodera/Content/Technical_Information/BT_Snoop_File_Format.htm
// for the official* documentation.
//
// NOTE: This class is not thread-safe.
//
// * The original BTSnoop format comes from Symbian OS and was later adopted by
// Frontline. Hence the official documentation is in Frontline's website.
class BTSnoopLogger final {
 public:
  BTSnoopLogger() = default;
  ~BTSnoopLogger() = default;

  // Creates a BTSnoop log file at the given |path|. If the specified file does
  // not exist this method creats it. If the file does exist and |truncate| is
  // true, this method truncates the file to zero. Returns false if an error
  // occurs or if this method was already called successfully on this instance
  // before, otherwise returns true.
  bool Initialize(const std::string& path, bool truncate = true);

  // Writes the contents of the given buffer in a packet record. The parameters
  // are:
  //   - |is_received|: True if the host received the packet from the controller
  //                    False if the packet was sent from the host to the
  //                    controller.
  //
  //   - |is_data|: True if this is a data packet. False if this is a
  //                command/event packet.
  //
  // Returns false if there is an error while writing or if Initialize has not
  // been called successfully before, otherwise returns true.
  bool WritePacket(const ByteBuffer& packet_data, bool is_received, bool is_data);

 private:
  // FD to the log file.
  fxl::UniqueFD fd_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BTSnoopLogger);
};

}  // namespace common
}  // namespace bluetooth
