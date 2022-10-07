// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.inspect/cpp/markers.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/fidl/cpp/wire/wire_types.h>
#include <lib/inspect/component/cpp/service.h>
#include <zircon/types.h>

#include <vector>

namespace inspect {

class TreeNameIterator final : public fidl::WireServer<fuchsia_inspect::TreeNameIterator> {
 public:
  // Start a server that deletes itself on unbind.
  static void StartSelfManagedServer(async_dispatcher_t* dispatcher,
                                     fidl::ServerEnd<fuchsia_inspect::TreeNameIterator>&& request,
                                     std::vector<std::string> names) {
    ZX_ASSERT(dispatcher);

    auto impl = std::unique_ptr<TreeNameIterator>(new TreeNameIterator(std::move(names)));
    auto* ptr = impl.get();
    auto binding_ref = fidl::BindServer(dispatcher, std::move(request), std::move(impl), nullptr);
    ptr->binding_.emplace(std::move(binding_ref));
  }

  // Get the next batch of names. Names are sent in batches of `kMaxTreeNamesListSize`,
  // which is defined with the rest of the FIDL protocol.
  void GetNext(GetNextCompleter::Sync& completer) {
    ZX_ASSERT(binding_.has_value());

    std::vector<fidl::StringView> converted_names;
    converted_names.reserve(names_.size());
    auto bytes_used = sizeof(fidl_message_header_t) + sizeof(fidl_vector_t);
    for (; current_index_ < names_.size(); current_index_++) {
      bytes_used += sizeof(fidl_string_t);
      bytes_used += FIDL_ALIGN(names_.at(current_index_).length());
      if (bytes_used > ZX_CHANNEL_MAX_MSG_BYTES) {
        break;
      }

      converted_names.emplace_back(fidl::StringView::FromExternal(names_.at(current_index_)));
    }

    completer.Reply(fidl::VectorView<fidl::StringView>::FromExternal(converted_names));
  }

 private:
  TreeNameIterator(std::vector<std::string>&& names) : names_(std::move(names)) {}
  cpp17::optional<fidl::ServerBindingRef<fuchsia_inspect::TreeNameIterator>> binding_;
  std::vector<std::string> names_;
  uint64_t current_index_ = 0;
};

void TreeServer::StartSelfManagedServer(Inspector inspector, TreeHandlerSettings settings,
                                        async_dispatcher_t* dispatcher,
                                        fidl::ServerEnd<fuchsia_inspect::Tree>&& request) {
  ZX_ASSERT(dispatcher);

  auto impl = std::unique_ptr<TreeServer>(
      new TreeServer(std::move(inspector), std::move(settings), dispatcher));

  auto* impl_ptr = impl.get();
  auto binding_ref = fidl::BindServer(dispatcher, std::move(request), std::move(impl), nullptr);
  impl_ptr->binding_.emplace(std::move(binding_ref));
}

void TreeServer::GetContent(GetContentCompleter::Sync& completer) {
  ZX_ASSERT(binding_.has_value());

  fidl::Arena arena;
  auto content_builder = fuchsia_inspect::wire::TreeContent::Builder(arena);
  fuchsia_mem::wire::Buffer buffer;
  const auto& primary_behavior = settings_.snapshot_behavior.PrimaryBehavior();
  const auto& failure_behavior = settings_.snapshot_behavior.FailureBehavior();
  using behavior_types = TreeServerSendPreference::Type;

  if (primary_behavior == behavior_types::Frozen) {
    auto maybe_vmo_frozen = inspector_.FrozenVmoCopy();
    if (maybe_vmo_frozen.has_value()) {
      buffer.vmo = std::move(maybe_vmo_frozen.value());
    } else if (failure_behavior.has_value() && *failure_behavior == behavior_types::Live) {
      buffer.vmo = inspector_.DuplicateVmo();
    } else {
      auto maybe_vmo_copied = inspector_.CopyVmo();
      if (maybe_vmo_copied.has_value()) {
        buffer.vmo = std::move(maybe_vmo_copied.value());
      } else {
        buffer.vmo = inspector_.DuplicateVmo();
      }
    }
  } else if (primary_behavior == behavior_types::Live) {
    buffer.vmo = inspector_.DuplicateVmo();
  } else {
    auto maybe_vmo_copied = inspector_.CopyVmo();
    if (maybe_vmo_copied.has_value()) {
      buffer.vmo = std::move(maybe_vmo_copied.value());
    } else {
      buffer.vmo = inspector_.DuplicateVmo();
    }
  }

  content_builder.buffer(std::move(buffer));
  completer.Reply(content_builder.Build());
}

void TreeServer::ListChildNames(ListChildNamesRequestView request,
                                ListChildNamesCompleter::Sync& completer) {
  ZX_ASSERT(binding_.has_value());

  TreeNameIterator::StartSelfManagedServer(
      executor_.dispatcher(), std::move(request->tree_iterator), inspector_.GetChildNames());
}

void TreeServer::OpenChild(OpenChildRequestView request, OpenChildCompleter::Sync& completer) {
  ZX_ASSERT(binding_.has_value());

  executor_.schedule_task(
      inspector_.OpenChild(std::string(request->child_name.get()))
          .and_then([request = std::move(request->tree), this](Inspector& inspector) mutable {
            TreeServer::StartSelfManagedServer(inspector, settings_, executor_.dispatcher(),
                                               std::move(request));
          }));
}

}  // namespace inspect
