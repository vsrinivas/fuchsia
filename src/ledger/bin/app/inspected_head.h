// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_INSPECTED_HEAD_H_
#define SRC_LEDGER_BIN_APP_INSPECTED_HEAD_H_

#include <lib/fit/function.h>

#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

// Represents to Inspect a head. Because a head is just a commit ID instances of this class expose
// what they need to expose to Inspect simply by just existing and maintaining an
// |inspect_deprecated::Node| in Inspect's hierarchy.
class InspectedHead final {
 public:
  explicit InspectedHead(inspect_deprecated::Node node);
  InspectedHead(const InspectedHead&) = delete;
  InspectedHead& operator=(const InspectedHead&) = delete;
  ~InspectedHead();

  void SetOnDiscardable(fit::closure on_discardable);
  bool IsDiscardable() const;

  fit::closure CreateDetacher();

 private:
  inspect_deprecated::Node node_;
  fit::closure on_discardable_;
  // TODO(nathaniel): This integer should be replaced with a (weak-pointer-less in this case)
  // TokenManager.
  int64_t outstanding_detachers_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_INSPECTED_HEAD_H_
