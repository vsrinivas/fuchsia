# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ffx component destroy /core/session-manager/session:session/workstation_session/login_shell/ermine_shell/elements:bouncing_box
ffx component create /core/session-manager/session:session/workstation_session/login_shell/ermine_shell/elements:bouncing_box fuchsia-pkg://fuchsia.com/bouncing_box#meta/bouncing_box.cm
ffx component start /core/session-manager/session:session/workstation_session/login_shell/ermine_shell/elements:bouncing_box