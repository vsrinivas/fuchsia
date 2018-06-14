// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace lockdep {

// Utility type to capture a reference to a global variable in the type system.
// This type can capture an lvalue reference to an object with static storage
// duration, either external or internal linkage.
template <typename T, T& Reference>
struct GlobalReference {};

// Utility type that returns the value type of a GlobalReference and passes
// other types through unchanged.
template <typename T>
struct RemoveGlobalReferenceType {
    using Type = T;
};
template <typename T, T& Reference>
struct RemoveGlobalReferenceType<GlobalReference<T, Reference>> {
    using Type = T;
};

// Alias to simplify type expressions for RemoveGlobalReferenceType.
template <typename T>
using RemoveGlobalReference = typename RemoveGlobalReferenceType<T>::Type;

} // namespace lockdep
