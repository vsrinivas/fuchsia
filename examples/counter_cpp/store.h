// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_EXAMPLES_COUNTER_CPP_STORE_H_
#define APPS_MODULAR_EXAMPLES_COUNTER_CPP_STORE_H_

#include <iterator>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"

namespace modular_example {

// TODO(jimbe) The code in this modular_example namespace is solely for the
// examples and needs to be factored into its own file.

// Subjects
constexpr char kDocId[] =
    "http://google.com/id/dc7cade7-7be0-4e23-924d-df67e15adae5";

// Property labels
constexpr char kCounterKey[] = "http://schema.domokit.org/counter";
constexpr char kSenderKey[] = "http://schema.org/sender";
constexpr char kJsonSegment[] = "counters";
constexpr char kJsonPath[] = "/counters";

class Counter {
 public:
  Counter() = default;

  Counter(const rapidjson::Value& name, const rapidjson::Value& value);

  rapidjson::Document ToDocument(const std::string& module_name);

  bool is_valid() { return counter >= 0; }

  // Remember where this data came from. This is particularly useful when
  // a story is rehydrated to ensure that everything restarts properly.
  std::string sender;

  // This is the module's "data". The value is incremented and sent back to the
  // Link whenever a message is received with a higher value.
  int counter = -1;
};

class Store : modular::LinkWatcher {
 public:
  using Callback = std::function<void()>;

  explicit Store(std::string module_name);

  void Initialize(fidl::InterfaceHandle<modular::Link> link);

  void AddCallback(Callback c);

  void Stop();

  void ModelChanged();

  void MarkDirty() { dirty_ = true; }

  bool terminating() { return terminating_; }

  static modular_example::Counter ParseCounterJson(
      const std::string& json,
      const std::string& module_name);

  modular_example::Counter counter;

 private:
  // |LinkWatcher|
  void Notify(const fidl::String& json) override;

  // Process an update from the Link and write it to our local copy.
  // The update is ignored if:
  //   - it's missing the desired document.
  //   - the data in the update is stale (can happen on rehydrate).
  void ApplyLinkData(const std::string& json);

  void SendIfDirty();

  std::string module_name_;

  std::vector<Callback> callbacks_;

  fidl::Binding<LinkWatcher> watcher_binding_;

  fidl::InterfacePtr<modular::Link> link_;

  // True if there is data pending to send to the link, otherwise false.
  bool dirty_{};
  bool terminating_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(Store);
};

}  // namespace modular_example

#endif  // APPS_MODULAR_EXAMPLES_COUNTER_CPP_STORE_H_
