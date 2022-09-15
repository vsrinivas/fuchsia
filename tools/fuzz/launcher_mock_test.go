// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"fmt"
	"io"
	"testing"
)

type mockLauncher struct {
	running           bool
	shouldFailToStart bool
	shouldExitEarly   bool

	// Failure modes that will be passed on to the Connector
	shouldFailToConnectCount uint
	shouldFailToExecuteCount uint

	// We need a reference to this to pass to NewMockConnector
	testEnv *testing.T
}

func NewMockLauncher(t *testing.T) *mockLauncher {
	return &mockLauncher{testEnv: t}
}

func (l *mockLauncher) Prepare() error {
	return nil
}

func (l *mockLauncher) Start() (Connector, error) {
	if l.running {
		return nil, fmt.Errorf("Start called on already-running Launcher")
	}

	if l.shouldFailToStart {
		return nil, fmt.Errorf("Intentionally broken Launcher")
	}

	l.Prepare()
	l.running = !l.shouldExitEarly

	conn := NewMockConnector(l.testEnv)
	conn.shouldFailToConnectCount = l.shouldFailToConnectCount
	conn.shouldFailToExecuteCount = l.shouldFailToExecuteCount
	return conn, nil
}

func (l *mockLauncher) IsRunning() (bool, error) {
	return l.running, nil
}

func (l *mockLauncher) Kill() error {
	if !l.running {
		return fmt.Errorf("Kill called on stopped Launcher")
	}
	l.running = false
	return nil
}

func (l *mockLauncher) GetLogs(out io.Writer) error {
	io.WriteString(out, "system log\n")
	return nil
}
