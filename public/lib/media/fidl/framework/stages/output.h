// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_OUTPUT_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_OUTPUT_H_

#include "apps/media/services/framework/models/demand.h"
#include "apps/media/services/framework/packet.h"
#include "apps/media/services/framework/payload_allocator.h"
#include "apps/media/services/framework/refs.h"

namespace mojo {
namespace media {

class Stage;
class Engine;
class Input;

// Represents a stage's connector to an adjacent downstream stage.
class Output {
 public:
  Output();

  ~Output();

  // The input to which this output is connected.
  const InputRef& mate() const { return mate_; }

  // Establishes a connection.
  void Connect(const InputRef& input);

  // Breaks a connection. Called only by the engine.
  void Disconnect() { mate_ = nullptr; }

  // Determines whether the output is connected to an input.
  bool connected() const { return static_cast<bool>(mate_); }

  // The connected input.
  Input& actual_mate() const;

  // Sets the allocator the output must use to copy the payload of output
  // packets. This is used when the connected input insists that a specific
  // allocator be used, but the stage can't use it.
  void SetCopyAllocator(PayloadAllocator* copy_allocator);

  // Demand signalled from downstream, or kNegative if the downstream input
  // is currently holding a packet.
  Demand demand() const;

  // Supplies a packet to mate. Called only by Stage::Update implementations.
  void SupplyPacket(PacketPtr packet, Engine* engine) const;

  // Updates packet demand. Called only by Input instances.
  bool UpdateDemandFromInput(Demand demand);

  // Flushes retained media.
  void Flush();

 private:
  InputRef mate_;
  Demand demand_;
  PayloadAllocator* copy_allocator_;
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_OUTPUT_H_
