// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_ANNOTATIONS_H_
#define SRC_MODULAR_BIN_SESSIONMGR_ANNOTATIONS_H_

#include <fuchsia/modular/cpp/fidl.h>

#include <vector>

namespace modular::annotations {

using Annotation = fuchsia::modular::Annotation;

// Merges the annotations from `b` onto `a`.
//
// * If `a` and `b` contain an annotation with the same key, the result will contain the one from
//   `b`, effectively overwriting it, then:
// * Annotations with a null value are omitted from the result.
// * Order is not guaranteed.
std::vector<Annotation> Merge(std::vector<Annotation> a, std::vector<Annotation> b);

// Helper function for translating annotation values to types ingestable by Inpect framework.
// TODO(fxbug.dev/37645): Template this to return the proper properties
std::string ToInspect(const fuchsia::modular::AnnotationValue& value);
}  // namespace modular::annotations

#endif  // SRC_MODULAR_BIN_SESSIONMGR_ANNOTATIONS_H_
