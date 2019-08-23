// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_INSPECTED_HEAD_H_
#define SRC_LEDGER_BIN_APP_INSPECTED_HEAD_H_

#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include "src/lib/fxl/macros.h"

namespace ledger {

// Represents to Inspect a head. Because a head is just a commit ID instances of this class expose
// what they need to expose to Inspect simply by just existing and maintaining an
// |inspect_deprecated::Node| in Inspect's hierarchy.
class InspectedHead final {
 public:
  explicit InspectedHead(inspect_deprecated::Node node);
  ~InspectedHead();

  void set_on_empty(fit::closure on_empty_callback);

  fit::closure CreateDetacher();

 private:
  inspect_deprecated::Node node_;
  fit::closure on_empty_callback_;
  // TODO(nathaniel): This integer should be replaced with a (weak-pointer-less in this case)
  // TokenManager.
  int64_t outstanding_detachers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectedHead);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_INSPECTED_HEAD_H_
