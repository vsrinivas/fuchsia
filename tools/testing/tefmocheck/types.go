// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import "go.fuchsia.dev/fuchsia/tools/testing/runtests"

// TestingOutputs are the outputs of a testing Swarming task that are analyzed by a FailureModeCheck.
type TestingOutputs struct {
	TestSummary     *runtests.TestSummary
	SwarmingSummary *SwarmingTaskSummary
	SerialLog       []byte
	SwarmingOutput  []byte
	Syslog          []byte
}

// LogType can be used to specify which logs a check should apply to.
type LogType string

const (
	SerialLogType      LogType = "serial_log"
	SwarmingOutputType LogType = "swarming_output"
	SyslogType         LogType = "syslog"
)

// LogBlock defines a section of a log and can be used to exclude checks from that block.
type LogBlock struct {
	StartString string
	EndString   string
}

// FailureModeCheck checks whether a failure mode appears.
type FailureModeCheck interface {
	// Check analyzes TestingOutputs and returns true if the failure mode was detected.
	Check(*TestingOutputs) bool
	// Name is the name of this check.
	Name() string
}
