// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gflags/gflags.h>

#include "src/connectivity/overnet/lib/embedded/basic_overnet_embedded.h"
#include "src/connectivity/overnet/lib/embedded/omdp_nub.h"
#include "src/connectivity/overnet/lib/embedded/stream_server.h"
#include "src/connectivity/overnet/lib/embedded/udp_nub.h"
#include "src/connectivity/overnet/lib/protocol/reliable_framer.h"

DEFINE_bool(udp, false, "Support Overnet over UDP");
DEFINE_bool(omdp, true,
            "Support OMDP discovery protocol for UDP communications");
DEFINE_string(unix_socket, "/tmp/ascendd.socket",
              "UNIX domain socket path for ascendd Overnet server");
DEFINE_string(verbosity, "INFO", "Verbosity level");
DEFINE_validator(
    verbosity, +[](const char* flag_name, const std::string& value) {
      return overnet::SeverityFromString(value).has_value();
    });

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  overnet::ScopedSeverity trace_severity(
      *overnet::SeverityFromString(FLAGS_verbosity));

  overnet::BasicOvernetEmbedded overnet_embedded;
  overnet::StreamServer<overnet::ReliableFramer> stream_server(
      &overnet_embedded, *overnet::IpAddr::Unix(FLAGS_unix_socket));

  // Optional services
  overnet::Optional<overnet::UdpNub> udp_nub;
  overnet::Optional<overnet::OmdpNub> omdp_nub;

  if (FLAGS_udp) {
    udp_nub.Reset(&overnet_embedded);
  }
  if (udp_nub.has_value() && FLAGS_omdp) {
    omdp_nub.Reset(&overnet_embedded, udp_nub.get());
  }

  return overnet_embedded.Run();
}
