// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_ANNOTATIONS_H_
#define SRC_MODULAR_BIN_SESSIONMGR_ANNOTATIONS_H_

#include <fuchsia/element/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/session/cpp/fidl.h>

#include <vector>

namespace modular::annotations {

// Separator between a |fuchsia::element::AnnotationKey| namespace and value when converting keys
// to and from a |fuchsia::modular::Annotation| that stores the key as a single string.
constexpr char kNamespaceValueSeparator = '|';

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

// Helper function for converting a fuchsia::modular::Annotation to a fuchsia::session::Annotation.
fuchsia::session::Annotation ToSessionAnnotation(const fuchsia::modular::Annotation& annotation);

// Helper function for converting a vector of fuchsia::modular::Annotations to a
// fuchsia::session::Annotations object.
fuchsia::session::Annotations ToSessionAnnotations(
    const std::vector<fuchsia::modular::Annotation>& annotations);

// Converts a |fuchsia::modular::Annotation| key to a |fuchsia::element::AnnotationKey|.
//
// If the key contains a separator from being previously converted from an element
// |AnnotationKey|, the key is parsed to extract a namespace and value. Otherwise, the
// resulting |AnnotationKey| uses the "global" namespace and the key for the value, as-is.
fuchsia::element::AnnotationKey ToElementAnnotationKey(const std::string& key);

// Converts a |fuchsia::modular::Annotation| to a equivalent |fuchsia::element::Annotation|.
fuchsia::element::Annotation ToElementAnnotation(const fuchsia::modular::Annotation& annotation);

// Converts a vector of |fuchsia::modular::Annotation|s to a vector of
// |fuchsia::element::Annotation|s.
std::vector<fuchsia::element::Annotation> ToElementAnnotations(
    const std::vector<fuchsia::modular::Annotation>& annotations);

}  // namespace modular::annotations

namespace session::annotations {

// Returns the equivalent |fuchsia::modular::Annotation| for the |fuchsia::session::Annotation|.
fuchsia::modular::Annotation ToModularAnnotation(const fuchsia::session::Annotation& annotation);

// Returns the equivalent list of |fuchsia::modular::Annotation|s for the
// |fuchsia::session::Annotations|.
//
// If |annotations| does not have |custom_annotations| set, returns an empty vector.
std::vector<fuchsia::modular::Annotation> ToModularAnnotations(
    const fuchsia::session::Annotations& annotations);

}  // namespace session::annotations

namespace element::annotations {

// The global key namespace, used for keys shared across all clients.
constexpr char kGlobalNamespace[] = "global";

// Converts a |fuchsia::element::AnnotationKey| to a |fuchsia::modular::Annotation| key.
//
// If the key namespace is "global", the value is returned as-is. Otherwise, the key namespace and
// value are escaped and joined with a separator.
std::string ToModularAnnotationKey(const fuchsia::element::AnnotationKey& key);

// Converts a |fuchsia::element::Annotation| to an equivalent |fuchsia::modular::Annotation|.
fuchsia::modular::Annotation ToModularAnnotation(const fuchsia::element::Annotation& annotation);

// Converts a vector of |fuchsia::modular::Annotation|s to a vector of equivalent
// |fuchsia::element::Annotation|s.
std::vector<fuchsia::modular::Annotation> ToModularAnnotations(
    const std::vector<fuchsia::element::Annotation>& annotations);

}  // namespace element::annotations

#endif  // SRC_MODULAR_BIN_SESSIONMGR_ANNOTATIONS_H_
