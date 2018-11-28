// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace.h"

namespace overnet {

class NullTrace final : public TraceRenderer {
 public:
  void Render(TraceOutput output) override {}
  void NoteParentChild(Op, Op) override {}
};

static NullTrace null_trace;

thread_local Scopes g_trace_scopes;
thread_local Op ScopedOp::current_ = Op::Invalid();
thread_local Severity ScopedSeverity::current_ = Severity::DEBUG;
thread_local TraceRenderer* ScopedRenderer::current_ = &null_trace;

std::ostream& operator<<(std::ostream& out, Module module) {
  switch (module) {
    case Module::MODULE_COUNT:
      return out << "<<ERROR>>";
    case Module::NUB:
      return out << "nub";
    case Module::PACKET_LINK:
      return out << "packet_link";
    case Module::DATAGRAM_STREAM:
      return out << "dg_stream";
    case Module::DATAGRAM_STREAM_SEND_OP:
      return out << "send_op";
    case Module::DATAGRAM_STREAM_RECV_OP:
      return out << "recv_op";
    case Module::DATAGRAM_STREAM_INCOMING_MESSAGE:
      return out << "inc_msg";
    case Module::PACKET_PROTOCOL:
      return out << "packet_protocol";
    case Module::BBR:
      return out << "bbr";
    case Module::ROUTER:
      return out << "router";
  }
}

std::ostream& operator<<(std::ostream& out, OpType type) {
  switch (type) {
    case OpType::INVALID:
      return out << "INVALID";
    case OpType::INCOMING_PACKET:
      return out << "IncomingPacket";
    case OpType::OUTGOING_REQUEST:
      return out << "OutgoingRequest";
    case OpType::LINEARIZER_INTEGRATION:
      return out << "LinearizerIntegration";
  }
}

std::ostream& operator<<(std::ostream& out, Op op) {
  return out << op.type() << "#" << op.somewhat_unique_id();
}

}  // namespace overnet
