// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ipcserver

import (
	"fmt"
	"strings"
	"sync"

	"fidl/bindings"

	"fuchsia/go/amber"

	"amber/daemon"
	"amber/pkg"
	"amber/source"

	"syscall/zx"
)

type ControlSrvr struct {
	daemonSrc  *daemon.DaemonProvider
	daemonGate sync.Once
	daemon     *daemon.Daemon
	pinger     *source.TickGenerator
	bs         bindings.BindingSet
}

func NewControlSrvr(d *daemon.DaemonProvider, r *source.TickGenerator) *ControlSrvr {
	go bindings.Serve()
	return &ControlSrvr{daemonSrc: d, pinger: r}
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
	if len(name) == 0 {
		return nil, fmt.Errorf("No name provided")
	}

	if name[0] != '/' {
		name = fmt.Sprintf("/%s", name)
	}

	ps := pkg.NewPackageSet()
	pkg := pkg.Package{Name: name, Version: *version}
	ps.Add(&pkg)

	c.initDaemon()
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

func (c *ControlSrvr) GetBlob(merkle string) error {
	if len(strings.TrimSpace(merkle)) == 0 {
		return fmt.Errorf("Supplied merkle root is empty")
	}

	c.initDaemon()
	return c.daemon.GetBlob(merkle)
}

func (c *ControlSrvr) Quit() {
	c.bs.Close()
}

func (c *ControlSrvr) Bind(ch zx.Channel) error {
	s := amber.ControlStub{Impl: c}
	_, err := c.bs.Add(&s, ch, nil)
	return err
}

func (c *ControlSrvr) initDaemon() {
	c.daemonGate.Do(func() {
		c.pinger.GenerateTick()
		c.daemon = c.daemonSrc.Daemon()
		c.pinger = nil
	})
}
