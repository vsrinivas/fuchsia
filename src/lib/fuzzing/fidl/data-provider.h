// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_DATA_PROVIDER_H_
#define SRC_LIB_FUZZING_FIDL_DATA_PROVIDER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/synchronization/thread_annotations.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "test-input.h"
#include "traced-instruction.h"

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::DataProvider;

}  // namespace

// Implementation of fuchsia.fuzzer.DataProvider.
//
// This class can be used to partition a single libFuzzer test input into inputs for multiple
// consumers. It is designed to be "fuzzer-stable", that is, inserting or removing bytes do not
// generally change how the input is partitioned.
//
// It is also designed to facilitate handcrafting corpus elements for sending data to multiple
// consumers, using Rust-style attributes,
//
// On startup, the fuchsia.fuzzer.LlvmFuzzer implementation must call |Configure| to provide this
// service with a VMO it can use to provide test inputs from libFuzzer. It also provides a list of
// labels designating other consumers. Providing them upon startup initialization allows the
// provider to partition the input even before other services have started in response to FIDL
// requests made by the fuzzer.
//
// Labels may contain any characters except '#', '[', and ']'.
//
// On starting up, the other consumers should discover the DataProvider and call |AddConsumer| with
// a label matching one provided by the fuzzer and a VMO to hold the test input. These consumers
// will be notified when data is available with the "OnDataAvailable" event.
//
// The test input is partitioned using the following rules, where "LABEL" corresponds to one of the
// previously provided labels.
//  1. Initially, data is written to the fuzzer-provided TestInput.
//  2. If the input contains a byte sequences like "##[LABEL]", it is mapped to "#[LABEL]" and then
//     skipped. (This allows the input to express all patterns, including labels).
//  3. Otherwise, if the input contains a byte sequence like "#[LABEL]", all subsequent data up to
//     the next such label or data end is written to the corresponding TestInput.
//
// For example, assuming |Configure| was called with labels like {"foo", "bar", "baz"} and the
// following input:
// 00000000  41 41 41 41 41 41 41 41  41 41 41 41 41 41 41 41  |AAAAAAAAAAAAAAAA|
// 00000000  41 23 5B 62 61 7A 5D 42  42 42 23 23 5B 62 61 72  |A#[baz]BBB##[bar|
// 00000000  5D 43 43 23 5B 66 6F 6F  5D 44 44 44 44 44 44 44  |]CC#[foo]DDDDDDD|
//
// The data would be partitioned as follows:
//  * The fuzzer would receive 17 bytes of "AAAAAAAAAAAAAAAAA".
//  * The "foo" consumer would receive 7 bytes of "DDDDDDD".
//  * The "bar" consumer would receive 0 bytes.
//  * The "baz" consumer would receive 11 bytes of "BBB#[bar]CC" (*not* 12 bytes of "BBB##[bar]CC").
//
class DataProviderImpl : public DataProvider {
 public:
  DataProviderImpl();
  virtual ~DataProviderImpl();

  fidl::InterfaceRequestHandler<DataProvider> GetHandler() { return bindings_.GetHandler(this); }

  // FIDL methods
  void Configure(zx::vmo vmo, fidl::VectorPtr<std::string> labels,
                 ConfigureCallback callback) override FXL_LOCKS_EXCLUDED(lock_);
  void AddConsumer(std::string label, zx::vmo vmo, AddConsumerCallback callback) override
      FXL_LOCKS_EXCLUDED(lock_);

  // Partitions the test input according to the class description above. Returns an error if it is
  // unable to signal consumers that data is ready, which is fatal.
  zx_status_t PartitionTestInput(const void *data, size_t size) FXL_LOCKS_EXCLUDED(lock_);

  // Signals all connected consumers that the current iteration is complete, i.e. they should not
  // use any more data from the test input. Returns an error if it is unable to signal consumers
  // to stop using data, which is fatal.
  zx_status_t CompleteIteration() FXL_LOCKS_EXCLUDED(lock_);

  // Returns the object to an initial state, i.e. ready for |Configure| to be called again.
  void Reset();

 protected:
  // Accessors for testing.
  virtual bool HasLabel(const std::string &label) FXL_LOCKS_EXCLUDED(lock_);
  virtual bool IsMapped(const std::string &label) FXL_LOCKS_EXCLUDED(lock_);

 private:
  // Bindings for connected clients.
  fidl::BindingSet<DataProvider> bindings_;

  // Lock to synchronize calls from engine and dispatcher threads.
  std::mutex lock_;

  // Shared memory regions into which test input data is partitioned.
  std::map<std::string, TestInput, std::less<>> inputs_ FXL_GUARDED_BY(lock_);

  // Label length bounds, to speed up scanning test input when partitioning.
  size_t max_label_length_ FXL_GUARDED_BY(lock_);

  // Blocks |PartitionTestInput| until |Configure| has completed.
  sync_completion_t sync_;
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_DATA_PROVIDER_H_
