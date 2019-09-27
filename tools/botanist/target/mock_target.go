// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"context"
	"io"
	"net"

	"go.fuchsia.dev/fuchsia/tools/build/api"
)

// MockTarget represents a mock target device to use for testing.
type MockTarget struct {
	nodename string
	serial   io.ReadWriteCloser
}

// NewMockTarget returns a new mock target with the given nodename.
func NewMockTarget(ctx context.Context, nodename string, serial io.ReadWriteCloser) (*MockTarget, error) {
	return &MockTarget{
		nodename: nodename,
		serial:   serial,
	}, nil
}

// Nodename returns the name of the node.
func (t *MockTarget) Nodename() string {
	return t.nodename
}

// IPv4Addr returns the IPv4 address of the node.
func (t *MockTarget) IPv4Addr() (net.IP, error) {
	var ip net.IP
	return ip, nil
}

// Serial returns the serial device associated with the target for serial i/o.
func (t *MockTarget) Serial() io.ReadWriteCloser {
	return t.serial
}

// SSHKey returns the private SSH key path associated with the authorized key to be paved.
func (t *MockTarget) SSHKey() string {
	return ""
}

// Start starts the target.
func (t *MockTarget) Start(ctx context.Context, images build.Images, args []string) error {
	return nil
}

// Restart restarts the target.
func (t *MockTarget) Restart(ctx context.Context) error {
	if t.serial != nil {
		defer t.serial.Close()
	}
	return nil
}

// Stop stops the target.
func (t *MockTarget) Stop(ctx context.Context) error {
	return ErrUnimplemented
}

// Wait waits for the target to stop.
func (t *MockTarget) Wait(ctx context.Context) error {
	return ErrUnimplemented
}
