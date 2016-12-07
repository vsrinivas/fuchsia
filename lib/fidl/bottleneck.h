// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_BOTTLENECK_H_
#define APPS_MODULAR_LIB_FIDL_BOTTLENECK_H_

#include <functional>
#include <vector>

namespace modular {

// A bottleneck is an asynchronous operation that can conceptually be
// executed concurrently, but multiple concurrent invocations just
// wait for a single underlying operation to finish.
//
// Examples for this are Stop() methods in various services: on
// Stop(), a service needs to asynchronously stop related services,
// write state to storage, etc. If another Stop() request arrives
// while the first one is in flight, the completion callback for the
// second request needs to be called when the first request finishes,
// but nothing else needs to be done to complete the second request.
//
// Duplicate requests for the same operation regularly arise in
// convergent dependency graphs (i.e. graphs that are acyclic but not
// trees). For example, when a story is taken down, it stops all its
// modules, but as a part of stopping a module itself might want to
// stop modules it had started (so it can be sure that it sees the
// final state of the link to it). Thus arise double stop requests to
// the dependent module, one from the story and another from the
// parent module.
//
// There are a few variants described below.
//
// * Back loaded bottleneck: The operation transitions into one common
//   destination state, regardless of what the initial state is. All
//   instances of the operation only require one underlying instance
//   to execute. Example is Module.Stop() and Story.Stop().
//
// * Front loaded bottleneck: The state at the start of the operation
//   determines the end state. If an additional instance of the
//   operation is requested while the first instance is in flight, the
//   operation is executed again after it finishes the first time.
//   Multiple requests while the operation is executing only require a
//   single operation to execute. Example is
//   StoryController.WriteStoryState().
//
// * Sequential bottleneck: Each instance of the operation must be
//   executed, but sequentially after the previous one finishes. (This
//   is not yet implemented here.)
//
// I just made up these names. Remember, they just mean what their
// definitions say they mean; the names don't mean anything.
class Bottleneck {
 public:
  using Result = std::function<void()>;
  using Operation = std::function<void(Result)>;

  // The kind of bottleneck as described above.
  enum Kind {
    FRONT,
    BACK,
  };

  // The underlying operation is defined as a pair of instance and
  // method. std::function<> would be an alternative if we need it.
  template<class Class>
  Bottleneck(Kind kind, Class* instance, void (Class::*method)(const Result&))
      : kind_(kind), operation_([instance, method](Result done) {
        (instance->*method)(done);
      }) {}

  // The invocation of the concurrent operation.
  void operator()(Result done);

 private:
  // Executes one call of the underlying operation.
  void Call();

  // Signals the completion of the underlying operation.
  void Done();

  const Kind kind_;
  const Operation operation_;

  // The result callbacks of the pending operation invocations.
  std::vector<Result> results_;

  // The index of the last result callback at the time of the latest
  // invocation of the underlying operation. When the underlying
  // operation completes, and cover_ is smaller than results_.size(),
  // this means there were new operation requests since the last
  // invocation of the underlying invocation. Depending on kind_ this
  // causes different completions of the bottleneck.
  unsigned int cover_{};
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_BOTTLENECK_H_
