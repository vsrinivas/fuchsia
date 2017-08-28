// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ipcserver

import (
	"fmt"
	"log"

	"fidl/bindings"

	"syscall/zx"
	"syscall/zx/mxerror"

	"garnet/amber/api/amber"

	"amber/daemon"
	"amber/pkg"
)

type ControlSrvr struct {
	daemon *daemon.Daemon
	stubs  []*bindings.Stub
}

func NewControlSrvr(d *daemon.Daemon) *ControlSrvr {
	return &ControlSrvr{daemon: d}
}

func (c *ControlSrvr) DoTest(in int32) (out string, err error) {
	r := fmt.Sprintf("Your number was %d\n", in)
	return r, nil
}

func (c *ControlSrvr) AddSrc(url string, rateLimit int32, pubKey string) (bool, error) {
	return true, nil
}

func (c *ControlSrvr) RemoveSrc(url string) (bool, error) {
	return true, nil
}

func (c *ControlSrvr) Check() (bool, error) {
	return true, nil
}

func (c *ControlSrvr) ListSrcs() ([]string, error) {
	return []string{}, nil
}

func (c *ControlSrvr) GetUpdate(name string, version *string) (*string, error) {
	d := ""
	if version == nil {
		version = &d
	}
	ps := pkg.NewPackageSet()
	pkg := pkg.Package{Name: name, Version: *version}
	ps.Add(&pkg)

	updates := c.daemon.GetUpdates(ps)
	res, ok := updates[pkg]
	if !ok {
		return nil, fmt.Errorf("No update available")
	}
	if res.Err != nil {
		return nil, res.Err
	}

	_, err := daemon.WriteUpdateToPkgFS(res)
	if err != nil {
		return nil, err
	}

	return &res.Update.Merkle, nil
}

func (c *ControlSrvr) Quit() {
	for _, s := range c.stubs {
		s.Close()
	}
	c.stubs = []*bindings.Stub{}
}

func (c *ControlSrvr) Bind(req amber.Control_Request) {
	s := req.NewStub(c, bindings.GetAsyncWaiter())
	c.stubs = append(c.stubs, s)
	go func(b *bindings.Stub) {
		for {
			if err := b.ServeRequest(); err != nil {
				if mxerror.Status(err) != zx.ErrPeerClosed {
					log.Printf("Request error %v \n", err)
				}
				break
			}
		}
	}(s)
}
