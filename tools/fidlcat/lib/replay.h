// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_REPLAY_H_
#define TOOLS_FIDLCAT_LIB_REPLAY_H_

#include "src/lib/fidl_codec/library_loader.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

// Class to replay a previously stored session. All the formating options can be used (for example
// the filtering of message).
class Replay : public EventDecoder {
 public:
  explicit Replay(SyscallDisplayDispatcher* dispatcher) : EventDecoder(dispatcher) {}

  // Dumps in text a binary protobuf file which contains a session.
  bool DumpProto(const std::string& proto_file_name);
  bool DumpProto(std::istream& is);

  // Replays a previously save session.
  bool ReplayProto(const std::string& proto_file_name);
  bool ReplayProto(const std::string& file_name, std::istream& is);
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_REPLAY_H_
