// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_handler.h"

#include <cstring>
#include <string>

#include "lib/ftl/logging.h"

namespace debugserver {

namespace {

// TODO(armansito): Update this as we add more features.
const char kSupportedFeatures[] = "QNonStop+;";

const char kQSupported[] = "qSupported";

bool PacketHasPrefix(const uint8_t* packet,
                     size_t packet_size,
                     const std::string& cmd_name) {
  if (packet_size < cmd_name.size())
    return false;

  return std::strncmp((const char*)packet, cmd_name.c_str(),
                      cmd_name.length()) == 0;
}

}  // namespace

bool CommandHandler::HandleCommand(const uint8_t* packet,
                                   size_t packet_size,
                                   const ResponseCallback& callback) {
  if (PacketHasPrefix(packet, packet_size, kQSupported))
    return HandleQSupported(packet, packet_size, callback);

  // TODO(armansito): Support other commands. Also figure out what response to
  // send when a command is not supported.
  FTL_LOG(ERROR) << "Command not supported: "
                 << std::string((const char*)packet, packet_size);
  return false;
}

bool CommandHandler::HandleQSupported(const uint8_t* packet,
                                      size_t packet_size,
                                      const ResponseCallback& callback) {
  // Verify the packet contents. We ignore the payload for now.
  size_t prefix_len = std::strlen(kQSupported);
  if (packet_size > prefix_len &&
      ((packet_size - prefix_len) < 2 || packet[prefix_len] != ':')) {
    FTL_LOG(ERROR) << "Malformed \"qSupported\" packet";
    return false;
  }

  // Respond with the supported features
  callback((const uint8_t*)kSupportedFeatures, std::strlen(kSupportedFeatures));
  return true;
}

}  // namespace debugserver
