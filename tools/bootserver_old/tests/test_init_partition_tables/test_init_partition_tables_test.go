// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/bootserver_old/tests"
)

func TestInitPartitionTables(t *testing.T) {
	_, cleanup := bootserver.StartQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")
	defer cleanup()

	logPattern := []bootserver.LogMatch{
		{"Received request from ", true},
		{"Proceeding with nodename ", true},
		{"Transfer starts", true},
		{"Transfer ends successfully", true},
		{"Issued reboot command to", false},
	}

	bootserver.CmdSearchLog(
		t, logPattern,
		bootserver.ToolPath(t, "bootserver"), "-n", bootserver.DefaultNodename,
		"--init-partition-tables", "/dev/class/block/000", "-1", "--fail-fast")
}
