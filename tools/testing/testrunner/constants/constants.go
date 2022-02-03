// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package constants

const (
	// FailedToReconnectMsg is logged attempting to reconnect to SSH after an error fails.
	FailedToReconnectMsg = "failed to reconnect over SSH"
	// FailedToRunSnapshotMsg is logged if testrunner fails to run snapshot over ssh.
	FailedToRunSnapshotMsg = "failed to run snapshot over ssh"
	// FailedToStartSerialTestMsg is logged if testrunner repeatedly fails to run a
	// test over serial and gives up.
	FailedToStartSerialTestMsg = "failed to start test over serial"
	// SkippedRunningTestsMsg is logged if testrunner fails early and skips running some tests.
	SkippedRunningTestsMsg = "skipped running some tests"

	// A directory that will be automatically archived on completion of a task.
	TestOutDirEnvKey = "FUCHSIA_TEST_OUTDIR"
)
