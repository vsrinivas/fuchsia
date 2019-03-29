// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class ExprValue;
class SymbolDataProvider;
class Type;

// Creates an ExprValue of the given type from the data at the given address.
// Issues the callback on completion. The type can be null (it will immediately
// call the callback with an error).
void ResolvePointer(fxl::RefPtr<SymbolDataProvider> data_provider,
                    uint64_t address, fxl::RefPtr<Type> type,
                    std::function<void(const Err&, ExprValue)> cb);

// Similar to the above but the pointer and type comes from the given
// ExprValue, which is assumed to be a pointer type. If it's not a pointer
// type, the callback will be issued with an error.
void ResolvePointer(fxl::RefPtr<SymbolDataProvider> data_provider,
                    const ExprValue& pointer,
                    std::function<void(const Err&, ExprValue)> cb);

// Ensures that the value is not a reference type (rvalue or regular). The
// result will be an ExprValue passed to the callback that has any reference
// types stripped.
//
// If the input ExprValue does not have a reference type, calls the callback
// immediately (from within the calling function's stack frame) with the input.
//
// If the value is a reference type, it will be resolved and the value will be
// the value of the referenced data.
void EnsureResolveReference(fxl::RefPtr<SymbolDataProvider> data_provider,
                            ExprValue value,
                            std::function<void(const Err&, ExprValue)> cb);

}  // namespace zxdb
