// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/redactor_factory.h"

#include <lib/inspect/cpp/vmo/types.h>

#include <limits>
#include <random>

namespace forensics::feedback {

int DefaultCacheIdFn() {
  int seed;
  zx_cprng_draw(&seed, sizeof(seed));
  std::default_random_engine rng(seed);
  std::uniform_int_distribution<int> dist(0, 7500);

  return dist(rng);
}

// Returns an IdentityRedactor if |enable_data_redaction| is false in |config|, otherwise returns a
// Redactor.
std::unique_ptr<RedactorBase> RedactorFromConfig(inspect::Node* root_node,
                                                 const BuildTypeConfig& config,
                                                 ::fit::function<int()> seed_cache_id) {
  std::unique_ptr<RedactorBase> redactor;
  auto redaction_enabled = root_node == nullptr ? inspect::BoolProperty()
                                                : root_node->CreateBool("redaction_enabled", false);
  if (config.enable_data_redaction) {
    redaction_enabled.Set(true);
    auto num_redaction_ids = root_node == nullptr ? inspect::UintProperty()
                                                  : root_node->CreateUint("num_redaction_ids", 0u);
    redactor = std::unique_ptr<RedactorBase>(
        new Redactor(seed_cache_id(), std::move(num_redaction_ids), std::move(redaction_enabled)));
  } else {
    redaction_enabled.Set(false);
    redactor = std::unique_ptr<RedactorBase>(new IdentityRedactor(std::move(redaction_enabled)));
  }

  return redactor;
}

}  // namespace forensics::feedback
