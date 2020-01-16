// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story/model/ledger_story_model_storage.h"

#include <lib/fidl/cpp/object_coding.h>  // for EncodeObject()/DecodeObject()
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

#include <string>
#include <vector>

#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/logging.h"
#include "src/modular/bin/sessionmgr/story/model/apply_mutations.h"
#include "src/modular/lib/ledger_client/promise.h"  // for fit::promise<> wrappers

using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular {

namespace {
// Synopsis of Ledger page structure:
//
// storymodel/                        - base prefix for all data for this story
//   device/<device id>               - key for device data for <device id>
//   shared/                          - prefix for data shared across devices

const char kStoryModelKeyPrefix[] = "storymodel/";
const char kDeviceKeyPrefix[] = "storymodel/device/";
// const char kSharedKeyPrefix[] = "shared/";

std::string MakeDeviceKey(const std::string& device_id) { return kDeviceKeyPrefix + device_id; }
}  // namespace

namespace {
// Encodes a FIDL table into a byte representation safe for persisting to
// storage.
template <class T>
std::vector<uint8_t> EncodeForStorage(T* table) {
  std::vector<uint8_t> encoded;
  // This can only fail if |table| contains handles. StoryModel and its fields
  // do not.
  FXL_CHECK(fidl::EncodeObject(table, &encoded, nullptr /* error_msg_out */) == ZX_OK);
  return encoded;
}

// Decodes bytes encoded by EncodeForStorage() into their corresponding FIDL
// table.
template <class T>
void DecodeFromStorage(std::vector<uint8_t> encoded, T* table) {
  // DecodeObject() takes a non-const pointer, even though it doesn't
  // modify the data.
  FXL_CHECK(fidl::DecodeObject(encoded.data(), encoded.size(), table,
                               nullptr /* error_msg_out */) == ZX_OK);
}
}  // namespace

LedgerStoryModelStorage::LedgerStoryModelStorage(LedgerClient* const ledger_client,
                                                 fuchsia::ledger::PageId page_id,
                                                 std::string device_id)
    : PageClient("LedgerStoryModelStorage", ledger_client, std::move(page_id),
                 kStoryModelKeyPrefix),
      device_id_(std::move(device_id)) {}

LedgerStoryModelStorage::~LedgerStoryModelStorage() = default;

// Helper functions to support OnPageChange() and OnPageDelete().
namespace {
// Appends to |commands| StoryModelMutation objects that, when applied to a
// StoryModel, reflect the device state in |device_state_bytes|.
void GenerateObservedMutationsForDeviceState(std::vector<uint8_t> device_state_bytes,
                                             std::vector<StoryModelMutation>* commands) {
  StoryModel model;
  DecodeFromStorage(std::move(device_state_bytes), &model);

  if (model.has_runtime_state()) {
    commands->resize(commands->size() + 1);
    commands->back().set_set_runtime_state(model.runtime_state());
  }
  if (model.has_visibility_state()) {
    commands->resize(commands->size() + 1);
    commands->back().set_set_visibility_state(model.visibility_state());
  }
}

void GenerateObservedMutationsForDeviceState(const fuchsia::mem::Buffer& buffer,
                                             std::vector<StoryModelMutation>* commands) {
  std::vector<uint8_t> bytes;
  FXL_CHECK(fsl::VectorFromVmo(buffer, &bytes));
  GenerateObservedMutationsForDeviceState(std::move(bytes), commands);
}
}  // namespace

void LedgerStoryModelStorage::OnChange(fuchsia::ledger::PageChange page_change,
                                       fuchsia::ledger::ResultState result_state,
                                       OnChangeCallback callback) {
  switch (result_state) {
    case fuchsia::ledger::ResultState::COMPLETED:
      ProcessCompletePageChange(std::move(page_change));
      break;
    case fuchsia::ledger::ResultState::PARTIAL_STARTED:
      partial_page_change_ = fuchsia::ledger::PageChange();
      // Continue to merging of values with |partial_page_change_|.
    case fuchsia::ledger::ResultState::PARTIAL_CONTINUED:
    case fuchsia::ledger::ResultState::PARTIAL_COMPLETED:
      // Merge the entries in |page_change| with |partial_page_change_|.
      partial_page_change_.changed_entries.insert(
          partial_page_change_.changed_entries.end(),
          std::make_move_iterator(page_change.changed_entries.begin()),
          std::make_move_iterator(page_change.changed_entries.end()));
      partial_page_change_.deleted_keys.insert(
          partial_page_change_.deleted_keys.end(),
          std::make_move_iterator(page_change.deleted_keys.begin()),
          std::make_move_iterator(page_change.deleted_keys.end()));

      if (result_state == fuchsia::ledger::ResultState::PARTIAL_COMPLETED) {
        ProcessCompletePageChange(std::move(partial_page_change_));
      }
  }

  callback(nullptr /* snapshot request */);
}

void LedgerStoryModelStorage::ProcessCompletePageChange(fuchsia::ledger::PageChange page_change) {
  std::vector<StoryModelMutation> commands;

  for (auto& entry : page_change.changed_entries) {
    auto key = to_string(entry.key);
    if (key == MakeDeviceKey(device_id_)) {
      FXL_CHECK(entry.value) << key << ": This key should never be deleted. Something is wrong.";
      // Read the value and generate equivalent StoryModelMutation commands.
      GenerateObservedMutationsForDeviceState(std::move(*entry.value), &commands);
    } else if (key.find(kDeviceKeyPrefix) == 0) {
      // This is device data from another device!
      // TODO(thatguy): Store it in the local StoryModel when we care about
      // observing these data.
    } else {
      FXL_LOG(FATAL) << "LedgerStoryModelStorage::OnPageChange(): key " << key
                     << " unexpected in the Ledger.";
    }
  }

  Observe(std::move(commands));

  // Don't do anything with deleted keys at this time.
}

void LedgerStoryModelStorage::OnPageConflict(Conflict* conflict) {
  // The default merge policy in LedgerClient is LEFT, meaning whatever value
  // was in the left branch for each key is taken.
  //
  // TODO(MF-157): LedgerClient breaks a single merge conflict for multiple
  // keys into on OnPageConflict() call per key. For a more advanced conflict
  // resolution policy, it is likely necessary to look at the conflict in full.
}

// Helper functions to support task construction in Execute().
namespace {
// Partitions |commands| into two vectors:
//
//   1) Those that mutate state that is device-local (ie, runtime state of the
//   story)
//
//   2) Those that mutate state that is shared among all devices (ie, the set
//   of mods)
struct PartitionedCommands {
  // These commands represent mutations that apply only to device-local state.
  std::vector<StoryModelMutation> device_commands;
  // And these apply to shared (cross-device) state.
  std::vector<StoryModelMutation> shared_commands;
};
PartitionedCommands PartitionCommandsForDeviceAndShared(std::vector<StoryModelMutation> commands) {
  PartitionedCommands partitioned_commands;

  for (auto& i : commands) {
    switch (i.Which()) {
      case StoryModelMutation::Tag::kSetRuntimeState:
      case StoryModelMutation::Tag::kSetVisibilityState:
        partitioned_commands.device_commands.push_back(std::move(i));
        break;
      case StoryModelMutation::Tag::Invalid:
        FXL_LOG(FATAL) << "Encountered invalid StoryModelMutation.";
        break;
    }
  }

  return partitioned_commands;
}

// TODO(thatguy): Move these functions to ledger_client/promise.h

// Reads the value in the given key and returns an object of type T. If |key|
// does not have a value, returns a default-constructed T.
template <class T>
fit::promise<T> ReadObjectFromKey(fuchsia::ledger::PageSnapshot* snapshot, const std::string& key) {
  return PageSnapshotPromise::GetInline(snapshot, key)
      .and_then([](const std::unique_ptr<std::vector<uint8_t>>& value) {
        if (!value) {
          return fit::ok(T());
        }

        T object;
        DecodeFromStorage(std::move(*value), &object);
        return fit::ok(std::move(object));
      });
}

// Writes |value| to |key|.
template <class T>
void WriteObjectToKey(fuchsia::ledger::Page* page, const std::string& key, T value) {
  auto bytes = EncodeForStorage(&value);
  // TODO(thatguy): Calculate if this value is too big for a FIDL message.  If
  // so, fall back on Page.CreateReferenceFromBuffer() and Page.PutReference().
  page->Put(to_array(key), std::move(bytes));
}

// Reads the latest device-local state, applies |commands| to it, and then
// writes it back to the Ledger.
//
// Store all the device-local state under a single key, and re-use
// a sparsely populated StoryModel table as our data structure for simplicity.
//
// The returned promise is resolved once calls to mutate the Page have
// returned.
fit::promise<> UpdateDeviceState(fuchsia::ledger::Page* page,
                                 fuchsia::ledger::PageSnapshot* snapshot,
                                 const std::string& device_id,
                                 std::vector<StoryModelMutation> commands) {
  // Task synopsis:
  //
  // 1) Read the current contents at |key| from the page snapshot.
  // 2) Apply |commands| to those contents.
  // 3) Write the new contents back to |key|.
  auto key = MakeDeviceKey(device_id);
  return ReadObjectFromKey<StoryModel>(snapshot, key)
      .and_then([page, key, commands = std::move(commands)](const StoryModel& current_value) {
        auto new_value = ApplyMutations(current_value, commands);
        WriteObjectToKey(page, key, std::move(new_value));
        return fit::ok();
      });
}

// Updates the shared state section of the ledger based on |commands|.
//
// The returned promise is resolved once calls to mutate the Page have
// returned.
fit::promise<> UpdateSharedState(fuchsia::ledger::Page* page,
                                 fuchsia::ledger::PageSnapshot* snapshot,
                                 std::vector<StoryModelMutation> commands) {
  // There is no shared state yet.
  return fit::make_ok_promise();
}
}  // namespace

fit::promise<> LedgerStoryModelStorage::Load() {
  // Synopsis of Load() task:
  //
  // 1) Read from device-local state and build commands.
  // 2) Scan the shared state and build commands.
  // 3) Wait for the above tasks and then issue all of the commands to
  // Observe().
  //
  // NOTE: currently we don't have any shared state, so we skip (2).

  struct State {
    fuchsia::ledger::PageSnapshotPtr page_snapshot;
    std::vector<StoryModelMutation> commands;
  };
  auto state = std::make_unique<State>();

  page()->GetSnapshot(state->page_snapshot.NewRequest(), {} /* key_prefix */,
                      nullptr /* watcher */);
  auto key = MakeDeviceKey(device_id_);
  auto read_promise =
      PageSnapshotPromise::GetInline(state->page_snapshot.get(), key)
          .and_then([state = state.get()](
                        const std::unique_ptr<std::vector<uint8_t>>& device_state_bytes) {
            if (device_state_bytes) {
              GenerateObservedMutationsForDeviceState(std::move(*device_state_bytes),
                                                      &state->commands);
            }
            return fit::ok();
          });

  return read_promise
      .and_then([this, state = state.get()]() -> fit::result<> {
        Observe(std::move(state->commands));
        return fit::ok();
      })
      // Keep |state| alive until execution reaches here.
      .inspect([state = std::move(state)](fit::result<>& r) {})
      .wrap_with(scope_);
}

fit::promise<> LedgerStoryModelStorage::Flush() {
  // The returned promise will block until all pending mutation opertaions have
  // resolved. These pending operations are also wrapped with |sequencer_| (in
  // Execute()), which applies this sequential behavior to promises it wraps.
  return fit::make_ok_promise().wrap_with(sequencer_);
}

fit::promise<> LedgerStoryModelStorage::Execute(std::vector<StoryModelMutation> commands) {
  // Synopsis of the Execute() task:
  //
  // 1) Start a Page transaction.
  // 2) Get a PageSnapshot.
  // 3) Partition |commands| into those affecting per-device state and shared
  // state and then update each partition in storage in parallel.
  // 4) Commit() if successful, and Rollback() if not.
  //
  // To take maximum advantage of FIDL pipelining and concurrency, do (1), (2),
  // and (3). Before (4), join on all the results and fail if
  // any of 1-3 failed.

  // Some state must outlast several of the fit::promise callbacks below.
  // Capture it in a struct on the heap, and then move ownership to a point
  // late enough in our promise by calling
  // fit::promise.inspect().
  struct State {
    fuchsia::ledger::PageSnapshotPtr page_snapshot;
  };
  auto state = std::make_unique<State>();

  return fit::make_promise([this, state = state.get(),
                            commands = std::move(commands)]() mutable -> fit::promise<> {
           page()->StartTransaction();
           page()->GetSnapshot(state->page_snapshot.NewRequest(), {} /* key_prefix */,
                               nullptr /* watcher */);

           // Partition up the commands into those that affect device-only
           // state, and those that affect shared (among all devices) state.
           auto [device_commands, shared_commands] =
               PartitionCommandsForDeviceAndShared(std::move(commands));

           // Dispatch the update commands.
           auto update_device_state_promise = UpdateDeviceState(
               page(), state->page_snapshot.get(), device_id_, std::move(device_commands));
           auto update_shared_state_promise =
               UpdateSharedState(page(), state->page_snapshot.get(), std::move(shared_commands));

           // Wait on all four pending promises. Fail if any one of them
           // result in an error.
           return fit::join_promises(std::move(update_device_state_promise),
                                     std::move(update_shared_state_promise))
               .and_then([](std::tuple<fit::result<>, fit::result<>>& results) -> fit::result<> {
                 auto [device_result, shared_result] = results;
                 if (device_result.is_error() || shared_result.is_error()) {
                   return fit::error();
                 }
                 return fit::ok();
               });
         })
      // Keep |state| alive until execution reaches here. It is not needed in
      // any subsequent continuation functions.
      .inspect([state = std::move(state)](fit::result<>& r) {})
      .and_then([page = page()] { page->Commit(); })
      .or_else([page = page()] {
        // Even if RollbackTransaction() succeeds, fail the overall task.
        page->Rollback();
        return fit::error();
      })
      .wrap_with(sequencer_)  // Waits until last Execute() is done.
      .wrap_with(scope_);     // Aborts if |this| is destroyed.
}

}  // namespace modular
