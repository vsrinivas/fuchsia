// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "abigen_generator.h"
#include "parser/parser.h"

bool process_comment(AbigenGenerator* parser, TokenStream& ts);
bool process_syscall(AbigenGenerator* parser, TokenStream& ts);
