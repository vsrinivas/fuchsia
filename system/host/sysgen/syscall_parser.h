// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "parser.h"
#include "generator.h"

bool process_comment(SysgenGenerator* parser, TokenStream& ts);
bool process_syscall(SysgenGenerator* parser, TokenStream& ts);
