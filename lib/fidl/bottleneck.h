// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_BOTTLENECK_H_
#define APPS_MODULAR_LIB_FIDL_BOTTLENECK_H_

#include <functional>
#include <vector>

namespace modular {

// A bottleneck is an asynchronous operation that can conceptually be
// executed concurrently, but multiple concurrent operations just wait
// for a single underlying operation to finish.
//
// Examples for this are Stop() methods in various services: on
// Stop(), a service needs to asynchronously stop related services,
// write state to storage, etc. If another Stop() request arrives
// while the first one is in flight, the completion callback for the
// second request needs to be called when the first request finishes.
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
// There are a few variants:
//
// * Back loaded bottleneck: The operation transitions into one common
//   destination state, regardless of what the initial state is. All
//   instances of the operation only require one underlying instance
//   to execute. Example is Module.Stop() and Story.Stop().
//
// * Front loaded bottleneck: The state at the start of the operation
//   determines the end state. Thus, if an additional instance of the
//   operation is requested while the first instance is in flight, the
//   operation is executed again after it finishes the first time.
//   Multiple requests while the operation is executing only require a
//   single operation to execute. Example is
//   StoryController.WriteStoryState().
//
// * Sequential bottleneck: Each instance of the operation must be
//   executed, but sequentially after the previous one finishes.
class Bottleneck {
 public:
  using Result = std::function<void()>;
  using Operation = std::function<void(Result)>;

  enum Kind {
    FRONT,
    BACK,
  };

  Bottleneck(Kind kind, Operation operation);

  template<class Class>
  Bottleneck(Kind kind, Class* instance, void (Class::*method)(const Result&))
      : kind_(kind), operation_([instance, method](Result done) {
        (instance->*method)(done);
      }) {}

  void operator()(Result done);

 private:
  void Call();
  void Done();

  const Kind kind_;
  const Operation operation_;
  unsigned int cover_{};
  std::vector<Result> results_;
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_BOTTLENECK_H_
