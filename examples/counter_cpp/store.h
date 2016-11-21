// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_EXAMPLES_COUNTER_CPP_STORE_H_
#define APPS_MODULAR_EXAMPLES_COUNTER_CPP_STORE_H_

#include <iterator>

#include "apps/modular/lib/document_editor/document_editor.h"
#include "apps/modular/services/story/link.fidl.h"
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
constexpr char kCounterLabel[] = "http://schema.domokit.org/counter";
constexpr char kSenderLabel[] = "http://schema.org/sender";

class Counter {
 public:
  Counter() = default;

  Counter(modular::DocumentEditor* editor);

  std::unique_ptr<modular::DocumentEditor> ToDocument(
      const std::string& module_name);

  bool is_valid() { return counter > 0; }

  // Remember where this data came from. This is particularly useful when
  // a story is rehydrated to ensure that everything restarts properly.
  std::string sender;

  // This is the module's "data". The value is incremented and sent back to the
  // Link whenever a message is received with a higher value.
  int counter = 0;
};

}  // namespace example

namespace modular {

class Store : public LinkWatcher {
 public:
  using Callback = std::function<void()>;

  Store(const std::string& module_name);

  void Initialize(fidl::InterfaceHandle<Link> link);

  void AddCallback(Callback c);

  void Stop();

  // See comments on Module2Impl in example-module2.cc.
  void Notify(FidlDocMap docs) override;

  // Process an update from the Link and write it to our local copy.
  // The update is ignored if:
  //   - it's missing the desired document.
  //   - the data in the update is stale (can happen on rehydrate).
  void ApplyLinkData(FidlDocMap docs);

  void ModelChanged();

  void MarkDirty() { dirty_ = true; }

  modular_example::Counter counter;

 private:
  void SendIfDirty();

  std::string module_name_;

  std::vector<Callback> callbacks_;

  fidl::Binding<LinkWatcher> watcher_binding_;

  fidl::InterfacePtr<Link> link_;

  // True if there is data pending to send to the link, otherwise false.
  bool dirty_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(Store);
};

}  // namespace modular

#endif  // APPS_MODULAR_EXAMPLES_COUNTER_CPP_STORE_H_
