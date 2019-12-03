// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/event_sender.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/service/cpp/service.h>

#include <set>

namespace inspect {

namespace {

class InspectTreeNameListService final : public fuchsia::inspect::TreeNameIterator {
 public:
  InspectTreeNameListService(std::vector<std::string> names,
                             fit::function<void(InspectTreeNameListService*)> done_callback)
      : names_(std::move(names)), done_callback_(std::move(done_callback)) {}

  void GetNext(GetNextCallback callback) override {
    std::vector<std::string> ret;
    size_t i = 0;
    for (; i < fuchsia::inspect::MAX_TREE_NAME_LIST_SIZE && name_offset_ < names_.size(); i++) {
      ret.emplace_back(std::move(names_[name_offset_]));
      name_offset_++;
    }

    callback(std::move(ret));

    // Close the connection only if the response was empty. We respond with an empty vector to let
    // the client know we will be disconnecting.
    if (i == 0) {
      done_callback_(this);
    }
  }

 private:
  size_t name_offset_ = 0;
  std::vector<std::string> names_;

  fit::function<void(InspectTreeNameListService*)> done_callback_;
};

class InspectTreeService final : public fuchsia::inspect::Tree {
 public:
  InspectTreeService(Inspector inspector, async_dispatcher_t* dispatcher,
                     fit::function<void(InspectTreeService*)> closer)
      : inspector_(std::move(inspector)), executor_(dispatcher), closer_(std::move(closer)) {}

  // Cannot be moved or copied.
  InspectTreeService(const InspectTreeService&) = delete;
  InspectTreeService(InspectTreeService&&) = delete;
  InspectTreeService& operator=(InspectTreeService&&) = delete;
  InspectTreeService& operator=(const InspectTreeService&) = delete;

  void GetContent(GetContentCallback callback) override {
    fuchsia::inspect::TreeContent ret;
    fuchsia::mem::Buffer buffer;
    buffer.vmo = inspector_.DuplicateVmo();
    buffer.vmo.get_size(&buffer.size);
    ret.set_buffer(std::move(buffer));
    callback(std::move(ret));
  }

  void ListChildNames(fidl::InterfaceRequest<fuchsia::inspect::TreeNameIterator> request) override {
    auto names = inspector_.GetChildNames();
    auto service = std::make_unique<InspectTreeNameListService>(
        std::move(names),
        [this](InspectTreeNameListService* ptr) { name_list_bindings_.RemoveBinding(ptr); });
    name_list_bindings_.AddBinding(std::move(service), std::move(request), executor_.dispatcher());
  }

  void OpenChild(std::string child_name,
                 fidl::InterfaceRequest<fuchsia::inspect::Tree> request) override {
    executor_.schedule_task(
        inspector_.OpenChild(std::move(child_name))
            .and_then([this, request = std::move(request)](Inspector& inspector) mutable {
              auto child = std::unique_ptr<InspectTreeService>(new InspectTreeService(
                  std::move(inspector), executor_.dispatcher(),
                  [this](InspectTreeService* ptr) { child_bindings_.RemoveBinding(ptr); }));
              child_bindings_.AddBinding(std::move(child), std::move(request));
            }));
  }

 private:
  Inspector inspector_;

  async::Executor executor_;
  fidl::BindingSet<fuchsia::inspect::Tree, std::unique_ptr<InspectTreeService>> child_bindings_;
  fidl::BindingSet<fuchsia::inspect::TreeNameIterator, std::unique_ptr<InspectTreeNameListService>>
      name_list_bindings_;

  // Calling this function unbinds the given service and destroys it immediately.
  fit::function<void(InspectTreeService*)> closer_;
};
}  // namespace

fidl::InterfaceRequestHandler<fuchsia::inspect::Tree> MakeTreeHandler(
    const inspect::Inspector* inspector, async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
    ZX_ASSERT(dispatcher);
  }

  auto bindings = std::make_unique<
      fidl::BindingSet<fuchsia::inspect::Tree, std::unique_ptr<InspectTreeService>>>();
  return [bindings = std::move(bindings), dispatcher,
          inspector](fidl::InterfaceRequest<fuchsia::inspect::Tree> request) mutable {
    bindings->AddBinding(std::make_unique<InspectTreeService>(
                             *inspector, dispatcher,
                             [binding_ptr = bindings.get()](InspectTreeService* ptr) {
                               binding_ptr->RemoveBinding(ptr);
                             }),
                         std::move(request));
  };
}

}  // namespace inspect
