// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fostr/fidl/fuchsia.feedback/amendments.h"

#include "garnet/public/lib/fostr/fidl_types.h"

namespace fuchsia {
namespace feedback {

std::ostream& operator<<(std::ostream& os, const Annotation& value) {
  using fidl::operator<<;
  os << ::fostr::Indent;
  os << ::fostr::NewLine << "key: " << value.key;
  os << ::fostr::NewLine << "value: " << value.value;
  return os << ::fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const ComponentData& value) {
  using fidl::operator<<;
  if (value.IsEmpty())
    return os << "<empty table>";

  os << ::fostr::Indent;
  if (value.has_namespace()) {
    // The field named "namespace", which is a reserved C++ keyword, is the reason why we need
    // amendments as fostr generates namespace() instead of namespace_(), cf. fxbug.dev/47480.
    os << ::fostr::NewLine << "namespace: " << value.namespace_();
  }
  if (value.has_annotations()) {
    os << ::fostr::NewLine << "annotations: " << ::fostr::PrintVector(value.annotations());
  }
  return os << ::fostr::Outdent;
}

}  // namespace feedback
}  // namespace fuchsia
