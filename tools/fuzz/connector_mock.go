// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"fmt"
	"strings"
)

// mockConnector is used in Fuzzer and Instance tests, and returned by mockLauncher.Start()
type mockConnector struct {
	connected             bool
	shouldFailToConnect   bool
	shouldFailToGetSysLog bool
}

func (c *mockConnector) Connect() error {
	if c.connected {
		return fmt.Errorf("Connect called when already connected")
	}

	if c.shouldFailToConnect {
		return fmt.Errorf("Intentionally broken Connector")
	}

	c.connected = true
	return nil
}

func (c *mockConnector) Close() {
	c.connected = false
}

func (c *mockConnector) Command(name string, args ...string) InstanceCmd {
	return &mockInstanceCmd{connector: c, name: name, args: args}
}

func (c *mockConnector) Get(targetSrc string, hostDst string) error {
	return nil
}

func (c *mockConnector) Put(hostSrc string, targetDst string) error {
	return nil
}

func (c *mockConnector) GetSysLog(pid int) (string, error) {
	if c.shouldFailToGetSysLog {
		return "syslog failure", fmt.Errorf("Intentionally broken Connector")
	}

	// TODO(fxbug.dev/45425): more realistic test data
	lines := []string{
		fmt.Sprintf("syslog for %d", pid),
		"[1234.5][klog] INFO: {{{0x41}}}",
	}
	return strings.Join(lines, "\n"), nil
}
