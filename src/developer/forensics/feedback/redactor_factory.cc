// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/redactor_factory.h"

#include <limits>
#include <random>

#include "src/lib/files/file.h"

namespace forensics::feedback {

int DefaultCacheIdFn() {
  int seed;
  zx_cprng_draw(&seed, sizeof(seed));
  std::default_random_engine rng(seed);
  std::uniform_int_distribution<int> dist(0, 7500);

  return dist(rng);
}

// Returns an IdentityRedactor if the file at |enable_flag_file| doesn't exist, otherwise return a
// Redactor.
std::unique_ptr<RedactorBase> RedactorFromConfig(const std::string& enable_flag_file,
                                                 ::fit::function<int()> seed_cache_id) {
  if (files::IsFile(enable_flag_file)) {
    return std::unique_ptr<RedactorBase>(new Redactor(seed_cache_id()));
  } else {
    return std::unique_ptr<RedactorBase>(new IdentityRedactor);
  }
}

}  // namespace forensics::feedback
