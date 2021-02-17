// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/inspect.h>

#include <fs/pseudo_dir.h>

zx::status<zx::vmo> ExposeInspector(const inspect::Inspector& inspector,
                                    const fbl::RefPtr<fs::PseudoDir>& dir);
