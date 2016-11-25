// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_EXAMPLES_TODO_GENERATOR_H_
#define APPS_LEDGER_EXAMPLES_TODO_GENERATOR_H_

#include <random>
#include <string>
#include <vector>

#include "lib/ftl/macros.h"

namespace todo {

class Generator {
 public:
  Generator(std::default_random_engine* rng,
            const std::vector<std::string>& positional_args);
  ~Generator();

  std::string Generate();

 private:
  std::default_random_engine* rng_;
  std::uniform_int_distribution<> action_distribution_;
  std::vector<std::string> actions_;
  std::uniform_int_distribution<> object_distribution_;
  std::vector<std::string> objects_;
  std::string tag_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Generator);
};

}  // namespace todo

#endif  // APPS_LEDGER_EXAMPLES_TODO_GENERATOR_H_
