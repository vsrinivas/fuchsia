// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"context"
	"io"
	"net"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
)

// Target represents a fuchsia instance.
type Target interface {
	// Nodename returns the name of the target node.
	Nodename() string

	// Serial returns the serial device associated with the target for serial i/o.
	Serial() io.ReadWriteCloser

	// SSHKey returns the private key corresponding an authorized SSH key of the target.
	SSHKey() string

	// Start starts the target.
	Start(context.Context, []bootserver.Image, []string) error

	// Stop stops the target.
	Stop(context.Context) error

	// Wait waits for the target to finish running.
	Wait(context.Context) error
}

// ConfiguredTarget represents a target that has static configuration.
type ConfiguredTarget interface {
	// Address returns the target's configured IP address.
	Address() net.IP
}

// Options represents lifecycle options for a target. The options will not necessarily make
// sense for all target types.
type Options struct {
	// Netboot gives whether to netboot or pave. Netboot here is being used in the
	// colloquial sense of only sending netsvc a kernel to mexec. If false, the target
	// will be paved. Ignored for QEMUTarget.
	Netboot bool

	// SSHKey is a private SSH key file, corresponding to an authorized key to be paved or
	// to one baked into a boot image.
	SSHKey string
}
