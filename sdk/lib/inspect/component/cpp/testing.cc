// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.inspect/cpp/markers.h>
#include <lib/fpromise/bridge.h>
#include <lib/inspect/component/cpp/testing.h>
#include <lib/inspect/cpp/reader.h>

#include <utility>

namespace inspect {
namespace testing {
namespace {
using inspect::internal::SnapshotTree;

fpromise::promise<SnapshotTree> SnapshotTreeFromTree(TreeClient&& tree,
                                                     async_dispatcher_t* dispatcher,
                                                     SnapshotTree&& root = {}) {
  fpromise::bridge<SnapshotTree> content_bridge;
  auto callback = [completer = std::move(content_bridge.completer), root = std::move(root)](
                      fidl::WireUnownedResult<fuchsia_inspect::Tree::GetContent>& result) mutable {
    Snapshot::Create(result.Unwrap()->content.buffer().vmo, &root.snapshot);
    completer.complete_ok(std::move(root));
  };

  tree->GetContent().ThenExactlyOnce(std::move(callback));
  return content_bridge.consumer.promise().and_then(
      [dispatcher = dispatcher, tree = std::move(tree)](
          SnapshotTree& current_level) mutable -> fpromise::promise<SnapshotTree> {
        auto child_name_endpoints = fidl::CreateEndpoints<fuchsia_inspect::TreeNameIterator>();
        return fpromise::make_promise([tree = std::move(tree),
                                       server_ends = std::move(child_name_endpoints),
                                       dispatcher = dispatcher]() mutable {
                 auto status = tree->ListChildNames(std::move(server_ends->server));
                 ZX_ASSERT_MSG(status.ok(), "tree->ListChildNames failed with bad status");
                 return fpromise::make_ok_promise(std::make_pair(
                     TreeNameIteratorClient(std::move(server_ends->client), dispatcher),
                     std::move(tree)));
               })
            .and_then([](std::pair<TreeNameIteratorClient, TreeClient>& args) {
              return fpromise::join_promises(ReadAllChildNames(args.first),
                                             fpromise::make_ok_promise(std::move(args.second)));
            })
            .and_then([](std::tuple<fpromise::result<std::vector<std::string>>,
                                    fpromise::result<TreeClient>>& args) {
              auto vec = std::get<0>(args).take_value();
              auto client = std::get<1>(args).take_value();
              return fpromise::make_ok_promise(std::make_pair(std::move(vec), std::move(client)));
            })
            .and_then([current_level = std::move(current_level), dispatcher = dispatcher](
                          std::pair<std::vector<std::string>, TreeClient>& args) mutable {
              std::vector<fpromise::promise<SnapshotTree>> child_promises;
              std::vector<std::string> child_names;
              for (const auto& n : args.first) {
                auto endpoints = fidl::CreateEndpoints<fuchsia_inspect::Tree>();
                auto status = args.second->OpenChild(fidl::StringView::FromExternal(n),
                                                     std::move(endpoints->server));
                ZX_ASSERT_MSG(status.ok(), "crashing at OpenChild status check");
                auto child_client = TreeClient(std::move(endpoints->client), dispatcher);
                child_promises.push_back(SnapshotTreeFromTree(std::move(child_client), dispatcher));
                child_names.push_back(n);
              }

              return fpromise::join_promises(
                  fpromise::join_promise_vector(std::move(child_promises)),
                  fpromise::make_ok_promise(std::move(current_level)),
                  fpromise::make_ok_promise(std::move(child_names)));
            })
            .and_then([](std::tuple<fpromise::result<std::vector<fpromise::result<SnapshotTree>>>,
                                    fpromise::result<SnapshotTree>,
                                    fpromise::result<std::vector<std::string>>>& results) {
              auto child_promises = std::get<0>(results).take_value();
              auto current = std::get<1>(results).take_value();
              auto child_names = std::get<2>(results).take_value();
              for (uint64_t i = 0; i < child_promises.size(); i++) {
                current.children[std::move(child_names.at(i))] = child_promises.at(i).take_value();
              }

              return fpromise::make_ok_promise(std::move(current));
            });
      });
}

}  // namespace

fpromise::promise<std::vector<std::string>> ReadAllChildNames(TreeNameIteratorClient& iter) {
  fpromise::bridge<std::vector<std::string>> bridge;
  auto callback =
      [completer = std::move(bridge.completer)](
          fidl::WireUnownedResult<fuchsia_inspect::TreeNameIterator::GetNext>& result) mutable {
        std::vector<std::string> resolved_names;
        ZX_ASSERT_MSG(result.ok(), "TreeNameIterator::GetNext failed: %s",
                      result.error().FormatDescription().c_str());
        for (const auto& name : result.Unwrap()->name) {
          resolved_names.push_back({std::cbegin(name), std::cend(name)});
        }
        completer.complete_ok(std::move(resolved_names));
      };

  using result_t = fpromise::result<std::vector<std::string>>;
  using promise_t = fpromise::promise<std::vector<std::string>>;
  iter->GetNext().ThenExactlyOnce(std::move(callback));
  return bridge.consumer.promise().then([&](result_t& result) mutable -> promise_t {
    if (!result.is_ok() || result.value().empty()) {
      return fpromise::make_ok_promise(std::vector<std::string>());
    }

    return ReadAllChildNames(iter).then([ret = result.take_value()](result_t& result) mutable {
      if (result.is_ok()) {
        for (auto& v : result.take_value()) {
          ret.emplace_back(std::move(v));
        }
      }
      return fpromise::make_ok_promise(std::move(ret));
    });
  });
}

fpromise::promise<Hierarchy> ReadFromTree(TreeClient& tree, async_dispatcher_t* dispatcher) {
  return SnapshotTreeFromTree(std::move(tree), dispatcher).and_then(internal::ReadFromSnapshotTree);
}

}  // namespace testing

}  // namespace inspect
