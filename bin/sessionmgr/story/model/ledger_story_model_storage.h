// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_MODEL_LEDGER_STORY_MODEL_STORAGE_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_MODEL_LEDGER_STORY_MODEL_STORAGE_H_

#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <lib/fit/scope.h>
#include <lib/fit/sequencer.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/sessionmgr/story/model/story_model_storage.h"

#include "peridot/lib/ledger_client/page_client.h"

namespace modular {

class LedgerClient;

// LedgerStoryModelStorage writes a StoryModel into a Ledger Page instance. It partitions
// the StoryModel into two sections:
//
// 1) Values that are scoped to this device (such as the Story's runtime state)
// 2) Values that are shared among all devices (such as the list of mod URLs)
//
// The two sections are stored in separate prefixes of the Ledger: (1) is
// prefixed using the device's id, and (2) is prefixed in a shared location.
class LedgerStoryModelStorage : public StoryModelStorage, PageClient {
 public:
  // Constructs a new instance which stores all data in |page_id| within
  // |ledger_client|'s Ledger. Scopes device-local state to a key namespace
  // therein with |device_id|.
  LedgerStoryModelStorage(LedgerClient* ledger_client,
                          fuchsia::ledger::PageId page_id,
                          std::string device_id);
  ~LedgerStoryModelStorage() override;

 private:
  // |PageClient|
  void OnPageChange(const std::string& key,
                    fuchsia::mem::BufferPtr value) override;

  // |PageClient|
  void OnPageDelete(const std::string& key) override;

  // |PageClient|
  void OnPageConflict(Conflict* conflict) override;

  // |StoryModelStorage|
  fit::promise<> Load() override;

  // |StoryModelStorage|
  fit::promise<> Flush() override;

  // |StoryModelStorage|
  fit::promise<> Execute(
      std::vector<fuchsia::modular::storymodel::StoryModelMutation> commands)
      override;

  const std::string device_id_;

  // With |scope_| is destroyed (which is when |this| is destructed), all
  // fit::promises created in Mutate() will be abandoned. This is important
  // because those promises capture |this| in their handler functions.
  fit::scope scope_;

  // All of the writes to the Ledger are sequenced: the fuchsia.ledger.Page API
  // dictates that only one transaction may be ongoing at a time. Each call to
  // Execute() results in a promise that calls StartTransaction() and Commit()
  // at its end. |sequencer_| is used to ensure that no subsequent Execute()
  // task begins before the previous has completed.
  fit::sequencer sequencer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerStoryModelStorage);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_MODEL_LEDGER_STORY_MODEL_STORAGE_H_
