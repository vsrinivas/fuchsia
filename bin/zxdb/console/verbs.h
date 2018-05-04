// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/console/command.h"

namespace zxdb {

// These functions add records for the verbs they support to the given map.
void AppendBreakpointVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendControlVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendMemoryVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendProcessVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendSymbolVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendSystemVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendThreadVerbs(std::map<Verb, VerbRecord>* verbs);

}  // namespace zxdb
