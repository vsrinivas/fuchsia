// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_KAZOO_ALIAS_WORKAROUND_H_
#define ZIRCON_TOOLS_KAZOO_ALIAS_WORKAROUND_H_

#include <string>

class SyscallLibrary;
class Type;

// If name is a special alias from alias_workarounds.fidl, create the appropriate Type into |type|
// and return true. Otherwise, return false.
bool AliasWorkaround(const std::string& name, const SyscallLibrary& library, Type* type);

#endif  // ZIRCON_TOOLS_KAZOO_ALIAS_WORKAROUND_H_
