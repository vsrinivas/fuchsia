// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/found_member.h"
#include "garnet/bin/zxdb/expr/found_name.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class CodeBlock;
class DataMember;
class ExprEvalContext;
class ExprValue;
class Identifier;
class InheritedFrom;

// Resolves a DataMember given a collection (class/struct/union) and a record
// for a variable within that collection. The data member must be on the class
// itself, not on a base class.
//
// Returns an error on failure, or puts the result in |out| on success.
//
// The DataMember may be null. If so, returns an error (this is so callers
// don't have to type check the inputs).
Err ResolveMember(const ExprValue& base, const DataMember* member,
                  ExprValue* out);

// Resolves a DataMember by name. This variant searches base classes for name
// matches.
//
// Returns an error if the name isn't found.
Err ResolveMember(const ExprValue& base, const Identifier& identifier,
                  ExprValue* out);

// The variant takes an ExprValue which is a pointer to the base/struct or
// class. Because it fetches memory it is asynchronous.
void ResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                            const ExprValue& base_ptr,
                            const FoundMember& found_member,
                            std::function<void(const Err&, ExprValue)> cb);

// Same as previous version but takes the name of the member to find.
// The callback also provides the DataMember corresponding to what the name
// matched.
void ResolveMemberByPointer(
    fxl::RefPtr<ExprEvalContext> context, const ExprValue& base_ptr,
    const Identifier& identifier,
    std::function<void(const Err&, fxl::RefPtr<DataMember>, ExprValue)> cb);

// Takes a Collection value and a base class inside of it, computes the value
// of the base class and puts it in *out. The base class must be a direct base
// class of the "value" collection, not an indirect base.
Err ResolveInherited(const ExprValue& value, const InheritedFrom* from,
                     ExprValue* out);

}  // namespace zxdb
