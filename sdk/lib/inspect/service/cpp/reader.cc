// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/service/cpp/reader.h>

using inspect::internal::SnapshotTree;

namespace inspect {

namespace {
fit::promise<SnapshotTree> SnapshotTreeFromTree(fuchsia::inspect::TreePtr tree) {
  fit::bridge<fuchsia::inspect::TreeContent> content_bridge;
  fuchsia::inspect::TreeNameIteratorPtr child_ptr;
  tree->GetContent(content_bridge.completer.bind());
  tree->ListChildNames(child_ptr.NewRequest(tree.dispatcher()));
  return fit::join_promises(content_bridge.consumer.promise_or(fit::error()),
                            ReadAllChildNames(std::move(child_ptr)))
      .and_then([tree = std::move(tree)](
                    std::tuple<fit::result<fuchsia::inspect::TreeContent>,
                               fit::result<std::vector<std::string>>>& result) mutable
                -> fit::promise<SnapshotTree> {
        auto& content = std::get<0>(result);
        auto& children = std::get<1>(result);

        SnapshotTree ret;
        if (!content.is_ok() || !children.is_ok() ||
            Snapshot::Create(content.take_value().buffer().vmo, &ret.snapshot) != ZX_OK) {
          return fit::make_result_promise<SnapshotTree>(fit::error());
        }

        // Sequence all child reads for depth-first traversal.
        fit::sequencer seq;
        std::vector<fit::promise<SnapshotTree>> child_promises;
        for (const auto& child_name : children.value()) {
          fuchsia::inspect::TreePtr child_ptr;
          tree->OpenChild(child_name, child_ptr.NewRequest(tree.dispatcher()));
          child_promises.emplace_back(SnapshotTreeFromTree(std::move(child_ptr)).wrap_with(seq));
        }

        return join_promise_vector(std::move(child_promises))
            .and_then(
                [ret = std::move(ret), tree = std::move(tree), children = children.take_value()](
                    std::vector<fit::result<SnapshotTree>>& results) mutable {
                  ZX_ASSERT(children.size() == results.size());

                  for (size_t i = 0; i < results.size(); i++) {
                    if (results[i].is_ok()) {
                      ret.children.emplace(std::move(children[i]), results[i].take_value());
                    }
                  }

                  return fit::ok(std::move(ret));
                });
      });
}
}  // namespace

fit::promise<std::vector<std::string>> ReadAllChildNames(
    fuchsia::inspect::TreeNameIteratorPtr iterator) {
  fit::bridge<std::vector<std::string>> bridge;

  iterator->GetNext(bridge.completer.bind());

  return bridge.consumer.promise_or(fit::error())
      .then([it = std::move(iterator)](fit::result<std::vector<std::string>>& result) mutable
            -> fit::promise<std::vector<std::string>> {
        if (!result.is_ok() || result.value().empty()) {
          return fit::make_ok_promise(std::vector<std::string>());
        }

        return ReadAllChildNames(std::move(it))
            .then(
                [ret = result.take_value()](fit::result<std::vector<std::string>>& result) mutable {
                  if (result.is_ok()) {
                    for (auto& v : result.take_value()) {
                      ret.emplace_back(std::move(v));
                    }
                  }
                  return fit::make_ok_promise(std::move(ret));
                });
      });
}

fit::promise<Hierarchy> ReadFromTree(fuchsia::inspect::TreePtr tree) {
  return fit::make_promise([tree = std::move(tree)]() mutable -> fit::promise<SnapshotTree> {
        return SnapshotTreeFromTree(std::move(tree));
      })
      .and_then(internal::ReadFromSnapshotTree);
}

}  // namespace inspect
