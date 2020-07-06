// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/bootserver_old/tests"
)

func TestWriteFirmwareNoType(t *testing.T) {
	instance, cleanup := bootserver.StartQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")
	defer cleanup()

	logPattern := []bootserver.LogMatch{
		{"Received request from ", true},
		{"Proceeding with nodename ", true},
		{"Transfer starts", true},
		{"Transfer ends successfully", true},
		{"Issued reboot command to", true},
	}

	bootserver.CmdSearchLog(
		t, logPattern,
		bootserver.ToolPath(t, "bootserver"), "-n", bootserver.DefaultNodename,
		"--firmware", bootserver.FirmwarePath(t), "-1", "--fail-fast")

	instance.WaitForLogMessage("netsvc: Running FIRMWARE Paver (firmware type '')")
}
