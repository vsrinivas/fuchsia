// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/stream_socket_link.h"

#include <fuchsia/overnet/protocol/cpp/fidl.h>

#include "src/connectivity/overnet/lib/labels/node_id.h"
#include "src/connectivity/overnet/lib/links/stream_link.h"
#include "src/connectivity/overnet/lib/protocol/fidl.h"
#include "src/connectivity/overnet/lib/vocabulary/optional.h"

namespace overnet {

namespace {
struct ValidatedArgs {
  NodeId peer;
  uint64_t remote_link_label;
};

class Link final : public StreamLink {
 public:
  Link(BasicOvernetEmbedded* app, const ValidatedArgs& args,
       uint64_t local_label, std::unique_ptr<StreamFramer> framer,
       Socket socket, Callback<void> destroyed)
      : StreamLink(app->endpoint(), args.peer, std::move(framer), local_label),
        reactor_(app->reactor()),
        socket_(std::move(socket)),
        destroyed_(std::move(destroyed)) {
    BeginRead();
  }

  ~Link() {
    if (socket_.IsValid()) {
      auto socket = std::move(socket_);
      reactor_->CancelIO(socket.get());
    }
  }

  void Emit(Slice slice, Callback<Status> done) override {
    auto status = socket_.Write(slice);
    if (status.is_error()) {
      done(status.AsStatus());
      return;
    }
    if (status->length() > 0) {
      reactor_->OnWrite(
          socket_.get(),
          StatusCallback(ALLOCATED_CALLBACK,
                         [this, slice = std::move(slice),
                          done = std::move(done)](Status status) mutable {
                           if (status.is_error()) {
                             done(std::move(status));
                             return;
                           }
                           Emit(std::move(slice), std::move(done));
                         }));
      return;
    }
    done(Status::Ok());
  }

 private:
  void BeginRead() {
    reactor_->OnRead(socket_.get(), [this](const Status& status) {
      if (status.is_error()) {
        return;
      }
      TimeStamp now = reactor_->Now();
      // Read some data. Choose a read size of maximum_segment_size + epsilon to
      // try and pull in full segments at a time.
      auto read = socket_.Read(maximum_segment_size() + 64);
      if (read.is_ok() && read->has_value()) {
        Process(now, std::move(**read));
        BeginRead();
      } else {
        OVERNET_TRACE(ERROR) << read;
      }
    });
  }

  HostReactor* const reactor_;
  Socket socket_;
  Callback<void> destroyed_;
};

class Handshake {
 public:
  static inline const std::string kGreetingString = "Fuchsia Socket Stream";

  Handshake(BasicOvernetEmbedded* app, Socket socket,
            std::unique_ptr<StreamFramer> framer, bool eager_announce,
            TimeDelta read_timeout, Callback<void> destroyed)
      : app_(app),
        socket_(std::move(socket)),
        framer_(std::move(framer)),
        link_label_(app->endpoint()->GenerateLinkLabel()),
        destroyed_(std::move(destroyed)),
        eager_announce_(eager_announce),
        read_timeout_(read_timeout) {
    if (eager_announce) {
      SendGreeting();
    }
    AwaitGreeting();
  }

 private:
  void DoneWriting(Status status) {
    OVERNET_TRACE(DEBUG) << "Finished writing stream handshake: " << status;
    if (status.is_error() && socket_.IsValid()) {
      app_->reactor()->CancelIO(socket_.get());
      socket_.Close();
    }
    Done();
  }

  void DoneReading(Status status) {
    OVERNET_TRACE(DEBUG) << "Finished reading stream handshake: " << status;
    if (status.is_error() && socket_.IsValid()) {
      app_->reactor()->CancelIO(socket_.get());
      socket_.Close();
    } else if (!eager_announce_) {
      SendGreeting();
    }
    Done();
  }

  void Done() {
    if (--dones_pending_ == 0) {
      if (socket_.IsValid()) {
        app_->reactor()->CancelIO(socket_.get());
        app_->endpoint()->RegisterPeer(validated_args_->peer);
        app_->endpoint()->RegisterLink(MakeLink<Link>(
            app_, *validated_args_, link_label_, std::move(framer_),
            std::move(socket_), std::move(destroyed_)));
      }

      delete this;
    }
  }

  void SendGreeting() {
    fuchsia::overnet::protocol::StreamSocketGreeting send_greeting;
    send_greeting.set_magic_string(kGreetingString);
    send_greeting.set_node_id(app_->node_id().as_fidl());
    send_greeting.set_local_link_id(link_label_);
    auto bytes = Encode(&send_greeting);
    if (bytes.is_error()) {
      DoneWriting(bytes.AsStatus());
      return;
    }

    SendBytes(framer_->Frame(std::move(*bytes)));
  }

  void SendBytes(Slice bytes) {
    app_->reactor()->OnWrite(
        socket_.get(),
        StatusCallback(ALLOCATED_CALLBACK, [this, bytes = std::move(bytes)](
                                               const Status& status) mutable {
          if (status.is_error()) {
            DoneWriting(status);
            return;
          }

          auto write_status = socket_.Write(bytes);
          if (write_status.is_error()) {
            DoneWriting(write_status.AsStatus());
            return;
          }
          if (write_status->length() > 0) {
            SendBytes(std::move(*write_status));
            return;
          }
          DoneWriting(Status::Ok());
        }));
  }

  void AwaitGreeting() {
    app_->reactor()->OnRead(socket_.get(), [this](const Status& status) {
      if (status.is_error()) {
        DoneReading(status);
        return;
      }

      auto read = socket_.Read(2 * framer_->maximum_segment_size);
      if (read.is_error()) {
        DoneReading(read.AsStatus());
        return;
      }
      if (!read->has_value()) {
        DoneReading(Status(StatusCode::UNKNOWN, "End of file handshaking"));
        return;
      }

      framer_->Push(std::move(**read));

      if (!ContinueReading()) {
        AwaitGreeting();
      }
    });
  }

  // Returns true if reading is done.
  bool ContinueReading() {
    auto frame = framer_->Pop();
    if (frame.is_error()) {
      DoneReading(frame.AsStatus().WithContext("Handshaking stream link"));
      return true;
    }
    if (!frame->has_value()) {
      if (read_timeout_ != TimeDelta::PositiveInf()) {
        skip_timeout_.Reset(app_->timer(), app_->timer()->Now() + read_timeout_,
                            [this](const Status& status) {
                              if (status.is_error()) {
                                return;
                              }
                              auto noise = framer_->SkipNoise();
                              OVERNET_TRACE(DEBUG)
                                  << "Skip input noise: " << noise;
                              ContinueReading();
                            });
      }
      return false;
    }

    auto decoded = Decode<fuchsia::overnet::protocol::StreamSocketGreeting>(
        std::move(**frame));
    if (decoded.is_error()) {
      DoneReading(decoded.AsStatus());
      return true;
    }

    if (!decoded->has_magic_string()) {
      DoneReading(Status(StatusCode::INVALID_ARGUMENT, "No magic string"));
      return true;
    }
    if (decoded->magic_string() != kGreetingString) {
      DoneReading(Status(StatusCode::INVALID_ARGUMENT, "Bad magic string"));
      return true;
    }
    if (!decoded->has_node_id()) {
      DoneReading(Status(StatusCode::INVALID_ARGUMENT, "No node id"));
      return true;
    }
    if (!decoded->has_local_link_id()) {
      DoneReading(Status(StatusCode::INVALID_ARGUMENT, "No local link id"));
      return true;
    }
    validated_args_.Reset(
        ValidatedArgs{decoded->node_id(), decoded->local_link_id()});
    DoneReading(Status::Ok());
    return true;
  }

  BasicOvernetEmbedded* const app_;
  Socket socket_;
  std::unique_ptr<StreamFramer> framer_;
  const uint64_t link_label_;
  int dones_pending_ = 2;
  Optional<ValidatedArgs> validated_args_;
  Callback<void> destroyed_;
  const bool eager_announce_;
  const TimeDelta read_timeout_;
  Optional<Timeout> skip_timeout_;
};
}  // namespace

void RegisterStreamSocketLink(BasicOvernetEmbedded* app, Socket socket,
                              std::unique_ptr<StreamFramer> framer,
                              bool eager_announce, TimeDelta read_timeout,
                              Callback<void> destroyed) {
  if (auto status = socket.SetNonBlocking(true); status.is_error()) {
    OVERNET_TRACE(WARNING)
        << "Failed to set non-blocking for stream link socket: " << status;
    return;
  }
  new Handshake(app, std::move(socket), std::move(framer), eager_announce,
                read_timeout, std::move(destroyed));
}

}  // namespace overnet
