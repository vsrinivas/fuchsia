// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/suggestion_channel.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace suggestion {

class AskSubscriber;

class AskChannel : public SuggestionChannel {
 public:
  // The given subscriber must outlive this channel. It is anticipated that it
  // will in fact own this channel.
  AskChannel(AskSubscriber* subscriber) : subscriber_(subscriber) {}

 protected:
  void DispatchOnAddSuggestion(
      const RankedSuggestion& ranked_suggestion) override;

  void DispatchOnRemoveSuggestion(
      const RankedSuggestion& ranked_suggestion) override;

 private:
  AskSubscriber* const subscriber_;
};

}  // namespace suggestion
}  // namespace maxwell
