// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/examples/todo/generator.h"

#include "lib/ftl/strings/concatenate.h"

namespace todo {
Generator::Generator(std::default_random_engine* rng,
                     const std::vector<std::string>& positional_args)
    : rng_(rng) {
  actions_ = {
      "acquire",   "cancel",      "consider",
      "draw",      "evaluate",    "celebrate",
      "find",      "identify",    "meet with",
      "plan",      "solve",       "study",
      "talk to",   "think about", "write an article about",
      "check out", "order",
  };
  action_distribution_ =
      std::uniform_int_distribution<>(0, actions_.size() - 1);

  objects_ = {
      "Christopher Columbus",
      "PHP",
      "a glass of wine",
      "a good book on C++",
      "a nice dinner out",
      "a sheep",
      "hipster bars south of Pigalle",
      "kittnes",
      "manganese",
      "some bugs",
      "staticly-typed programming languages",
      "the cryptographic primitives",
      "the espresso machine",
      "the law of gravity",
      "the neighbor",
      "the pyramids",
      "the society",
  };
  object_distribution_ =
      std::uniform_int_distribution<>(0, objects_.size() - 1);

  if (!positional_args.empty()) {
    tag_ = ftl::Concatenate({"[ ", positional_args[0], " ] "});
  }
}
Generator::~Generator() {}

std::string Generator::Generate() {
  return ftl::Concatenate({tag_, actions_[action_distribution_(*rng_)], " ",
                           objects_[object_distribution_(*rng_)]});
}

}  // namespace todo
