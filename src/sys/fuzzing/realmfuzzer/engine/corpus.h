// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_ENGINE_CORPUS_H_
#define SRC_SYS_FUZZING_REALMFUZZER_ENGINE_CORPUS_H_

#include <stddef.h>
#include <zircon/compiler.h>

#include <memory>
#include <random>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

// An alias to simplify passing around the shared corpora.
class Corpus;
using CorpusPtr = std::shared_ptr<Corpus>;

// Represents a set of fuzzing inputs. All inputs are held in memory, since writing them out to
// "disk" within the test runner framework would only write them to memfs, and lead to the same
// overall memory pressure. A corpus always includes an empty input, and so is never completely
// empty.
class Corpus final {
 public:
  Corpus();
  Corpus(Corpus&& other) noexcept { *this = std::move(other); }
  ~Corpus() = default;

  Corpus& operator=(Corpus&& other) noexcept;

  size_t num_inputs() const { return inputs_.size(); }
  size_t total_size() const { return total_size_; }

  static CorpusPtr MakePtr() { return std::make_shared<Corpus>(); }

  // Sets options. This will reset the PRNG.
  void Configure(const OptionsPtr& options);

  // Recusively walks the |root|-relative directories given by |dirs| and |Add|s the contents of the
  // files they contain.
  __WARN_UNUSED_RESULT zx_status_t LoadAt(const std::string& root,
                                          const std::vector<std::string>& dirs);

  // Like |LoadAt| with |root| defaulting to "/pkg".
  __WARN_UNUSED_RESULT zx_status_t Load(const std::vector<std::string>& dirs);

  // Adds the input to the corpus. Returns ZX_ERR_BUFFER_TOO_SMALL if the input exceeds the max size
  // specified by the options; ZX_OK otherwise.
  __WARN_UNUSED_RESULT zx_status_t Add(Input input);

  // Adds all inputs from the given |corpus| to this corpus. Returns ZX_ERR_INVALID_ARGS if |corpus|
  // is null, and ZX_ERR_BUFFER_TOO_SMALL if any/ input exceeds the max size specified by the
  // options. Otherwise, returns ZX_OK.
  zx_status_t Add(CorpusPtr corpus);

  // Returns true and the input at |offset| in the corpus via |out| if |offset| is less than the
  // number of inputs; otherwise returns false and sets |out| to an empty input.
  bool At(size_t offset, Input* out);

  // Returns a random element from the corpus via |out| This will always succeed, as this method
  // can pick the implicitly included empty element.
  void Pick(Input* out);

 private:
  // Reads the directory at |dirname| recursively and |Add|s the contents of each file.
  zx_status_t ReadDir(const std::string& dirname);

  // Reads the file at |filename| and |Add|s its contents.
  zx_status_t ReadFile(const std::string& filename);

  OptionsPtr options_;
  std::minstd_rand prng_;

  // TODO(fxbug.dev/84361): Currently, all inputs are held in memory. It may be desirable to store
  // some inputs on local storage when the corpus grows too large.
  std::vector<Input> inputs_;
  size_t total_size_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Corpus);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_ENGINE_CORPUS_H_
