// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_INPUT_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_INPUT_H_

#include "apps/media/services/framework/models/demand.h"
#include "apps/media/services/framework/packet.h"
#include "apps/media/services/framework/refs.h"

namespace mojo {
namespace media {

class Stage;
class Engine;
class Output;

// Represents a stage's connector to an adjacent upstream stage.
class Input {
 public:
  Input();

  ~Input();

  // The output to which this input is connected.
  const OutputRef& mate() const { return mate_; }

  // Establishes a connection.
  void Connect(const OutputRef& output);

  // Breaks a connection. Called only by the engine.
  void Disconnect() {
    DCHECK(!prepared_);
    mate_ = nullptr;
  }

  // Determines whether the input is connected to an output.
  bool connected() const { return static_cast<bool>(mate_); }

  // The connected output.
  Output& actual_mate() const;

  // Determines if the input is prepared.
  bool prepared() { return prepared_; }

  // Changes the prepared state of the input.
  void set_prepared(bool prepared) { prepared_ = prepared; }

  // A packet supplied from upstream.
  PacketPtr& packet_from_upstream() { return packet_from_upstream_; }

  // Updates mate's demand. Called only by Stage::Update implementations.
  void SetDemand(Demand demand, Engine* engine) const;

  // Updates packet_from_upstream. Return value indicates whether the stage for
  // this input should be added to the supply backlog. Called only by
  // Output instances.
  bool SupplyPacketFromOutput(PacketPtr packet);

  // Flushes retained media.
  void Flush();

 private:
  OutputRef mate_;
  bool prepared_;
  PacketPtr packet_from_upstream_;
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_INPUT_H_
