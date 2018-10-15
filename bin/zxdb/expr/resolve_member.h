// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <string>

#include "garnet/bin/zxdb/common/err.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class DataMember;
class ExprEvalContext;
class ExprValue;

// Resolves a DataMember given a base class and a record for a variable
// within that class. The data member must be on the class itself, not on
// a base class.
//
// Returns an error on failure, or puts the result in
// |out| on success.
//
// The DataMember may be null. If so, this class will return an error (this
// is so callers don't have to type check the inputs).
Err ResolveMember(const ExprValue& base, const DataMember* member,
                  ExprValue* out);

// Resolves a DataMember by name. This variant searches base classes for name
// matches.
//
// Returns an error if the name isn't found.
Err ResolveMember(const ExprValue& base, const std::string& member_name,
                  ExprValue* out);

// The variant takes an ExprValue which is a pointer to the base/struct or
// class. Because it fetches memory it is asynchronous.
void ResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                            const ExprValue& base_ptr, const DataMember* member,
                            std::function<void(const Err&, ExprValue)> cb);

// Same as previous version but takes the name of the member to find.
void ResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                            const ExprValue& base_ptr,
                            const std::string& member_name,
                            std::function<void(const Err&, ExprValue)> cb);

}  // namespace zxdb
