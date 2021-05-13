// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_SESSION_SESSION_CONSTANTS_H_
#define SRC_MODULAR_LIB_SESSION_SESSION_CONSTANTS_H_

// Components v1 URL for basemgr.
constexpr char kBasemgrV1Url[] = "fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx";

// Glob pattern for the path to basemgr's debug service when basemgr is running as a v1 component.
constexpr char kBasemgrDebugV1Glob[] = "/hub/c/basemgr.cmx/*/out/debug/basemgr";

// Glob pattern for the path to basemgr's debug service when basemgr is running as a session.
constexpr char kBasemgrDebugSessionGlob[] =
    "/hub-v2/children/core/children/session-manager/children/session:session/"
    "exec/expose/fuchsia.modular.internal.BasemgrDebug";

// Glob pattern for the path to the |Launcher| protocol exposed by a session.
constexpr char kLauncherGlob[] =
    "/hub-v2/children/core/children/session-manager/children/session:session/"
    "exec/expose/fuchsia.modular.session.Launcher";

#endif  // SRC_MODULAR_LIB_SESSION_SESSION_CONSTANTS_H_
