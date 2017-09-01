// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

class Handle;

// Deletes a |handle| made by MakeHandle() or DupHandle().
void DeleteHandle(Handle* handle);
