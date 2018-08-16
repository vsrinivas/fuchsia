// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <vector>

#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class ExprValue;
class SymbolDataProvider;
class Type;

// Gets the values from a range given an array of a given type. The end index
// is the index of one-past-tne-end of the desired data.
//
// Memory may be invalid. If so, the result vector will be truncated at the
// first element that's not completely valid. So the result may be less than
// the number of requested elements or empty. The operation will still count
// as successful in this case.
//
// If the whole operation fails due to a bad type or no connection to the
// debugged process, the error will be set and there will be no result.
void ResolveValueArray(fxl::RefPtr<SymbolDataProvider> data_provider,
                       const Type* value_type, uint64_t base_address,
                       size_t begin_index, size_t end_index,
                       std::function<void(const Err&, std::vector<ExprValue>)>);

// A more optimized version of the above for the common case of fetching
// byte data. The same rules about invalid memory and errors apply.
void ResolveByteArray(fxl::RefPtr<SymbolDataProvider> data_provider,
                      uint64_t base_address,
                      size_t begin_index, size_t end_index,
                      std::function<void(const Err&, std::vector<uint8_t>)>);

}  // namespace zxdb
