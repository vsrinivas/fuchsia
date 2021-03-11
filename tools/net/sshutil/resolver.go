// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"context"
	"net"
)

// Resolver produces a `net.Addr` for the sshutil connections. It abstracts
// over how the address is produced in order to support non-DNS based names,
// such as how Fuchsia devices can be discovered through a MAC-address based
// name.
type Resolver interface {
	Resolve(ctx context.Context) (net.Addr, error)
}

// ConstantAddrResolver allows a constant address to be resolved to itself.
type ConstantAddrResolver struct {
	Addr net.Addr
}

func (a ConstantAddrResolver) Resolve(ctx context.Context) (net.Addr, error) {
	return a.Addr, nil
}
