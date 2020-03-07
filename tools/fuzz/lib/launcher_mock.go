// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import "fmt"

type mockLauncher struct {
	running             bool
	shouldFailToStart   bool
	shouldFailToConnect bool
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
	l.running = true

	return &mockConnector{shouldFailToConnect: l.shouldFailToConnect}, nil
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
