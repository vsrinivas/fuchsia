// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_ABIGEN_SYSCALL_PARSER_H_
#define ZIRCON_TOOLS_ABIGEN_SYSCALL_PARSER_H_

#include "abigen_generator.h"
#include "parser/parser.h"

bool process_comment(AbigenGenerator* parser, TokenStream& ts);
bool process_syscall(AbigenGenerator* parser, TokenStream& ts);

#endif  // ZIRCON_TOOLS_ABIGEN_SYSCALL_PARSER_H_
