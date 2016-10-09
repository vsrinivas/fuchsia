// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "lib/ftl/macros.h"

namespace debugserver {

class Server;

// CommandHandler is responsible for handling GDB Remote Protocol commands.
class CommandHandler final {
 public:
  explicit CommandHandler(Server* server);
  ~CommandHandler() = default;

  // Handles the command packet |packet| of size |packet_size| bytes. Returns
  // false if the packet cannot be handled, otherwise returns true and calls
  // |callback|. Once a command is handled, |callback| will called with
  // |rsp| pointing to the contents of a response packet of size |rsp_size|
  // bytes. If |rsp_size| equals 0, then the response is empty.
  //
  // If this method returns false, then |callback| will never be called. If this
  // returns true, |callback| is guaranteed to be called exactly once.
  // |callback| can be called before HandleCommand returns.
  using ResponseCallback =
      std::function<void(const uint8_t* rsp, size_t rsp_size)>;
  bool HandleCommand(const uint8_t* packet,
                     size_t packet_size,
                     const ResponseCallback& callback);

 private:
  // Command handlers for each "letter" packet. We use underscores in the method
  // names to clearly delineate lowercase letters.
  bool Handle_H(const uint8_t* packet,
                size_t packet_size,
                const ResponseCallback& callback);
  bool Handle_q(const std::string& prefix,
                const uint8_t* params,
                size_t params_size,
                const ResponseCallback& callback);
  bool Handle_Q(const std::string& prefix,
                const uint8_t* params,
                size_t params_size,
                const ResponseCallback& callback);

  // qSupported
  bool HandleQuerySupported(const uint8_t* params,
                            size_t params_size,
                            const ResponseCallback& callback);
  // QNonStop
  bool HandleSetNonStop(const uint8_t* params,
                        size_t params_size,
                        const ResponseCallback& callback);

  // The root Server instance that owns us.
  Server* server_;  // weak

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandHandler);
};

}  // namespace debugserver
