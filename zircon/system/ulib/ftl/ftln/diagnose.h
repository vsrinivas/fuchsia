// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_FTL_FTLN_DIAGNOSE_H_
#define ZIRCON_SYSTEM_ULIB_FTL_FTLN_DIAGNOSE_H_

#include <string>

#include "ftlnp.h"

namespace ftl {

/// Search for known bad symptoms in a fully mounted FTL control block.
///
/// If no issues are found, returns empty stringm, otherwise returns human-readable diagnostic
/// of any discovered known issues.
std::string FtlnDiagnoseIssues(FTLN ftl);

}  // namespace ftl

#endif  // ZIRCON_SYSTEM_ULIB_FTL_FTLN_DIAGNOSE_H_
