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
	"amber/lg"
	"amber/pkg"
	"amber/source"

	"syscall/zx"
)

type ControlSrvr struct {
	daemonSrc   *daemon.DaemonProvider
	daemonGate  sync.Once
	daemon      *daemon.Daemon
	pinger      *source.TickGenerator
	bs          bindings.BindingSet
	actMon      *ActivationMonitor
	activations chan<- string
	compReqs    chan<- *completeUpdateRequest
	writeReqs   chan<- *startUpdateRequest
}

func NewControlSrvr(d *daemon.DaemonProvider, r *source.TickGenerator) *ControlSrvr {
	go bindings.Serve()
	a := make(chan string, 5)
	c := make(chan *completeUpdateRequest, 1)
	w := make(chan *startUpdateRequest, 1)
	m := NewActivationMonitor(c, w, a)
	go m.Do()

	return &ControlSrvr{
		daemonSrc:   d,
		pinger:      r,
		actMon:      m,
		activations: a,
		compReqs:    c,
		writeReqs:   w,
	}
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

func (c *ControlSrvr) getAndWaitForUpdate(name string, version *string, ch *zx.Channel) {
	res, err := c.downloadPkgMeta(name, version)
	if err != nil {
		ch.Close()
		return
	}
	lg.Log.Println("Package metadata retrieved, sending for additional processing")
	compReq := completeUpdateRequest{pkgData: res, replyChan: ch}
	c.compReqs <- &compReq
}

func (c *ControlSrvr) GetUpdateComplete(name string, version *string) (zx.Channel, error) {
	r, w, e := zx.NewChannel(0)
	if e != nil {
		lg.Log.Printf("Could not create channel")
		return 0, e
	}

	go c.getAndWaitForUpdate(name, version, &w)
	return r, nil
}

func (c *ControlSrvr) PackagesActivated(merkle []string) error {
	for _, m := range merkle {
		c.activations <- m
		lg.Log.Printf("Got package activation for %s\n", m)
	}
	return nil
}

func (c *ControlSrvr) downloadPkgMeta(name string, version *string) (*daemon.GetResult, error) {
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

	return res, nil
}

func (c *ControlSrvr) GetUpdate(name string, version *string) (*string, error) {
	res, err := c.downloadPkgMeta(name, version)
	if err != nil {
		return nil, err
	}

	wrtReq := startUpdateRequest{pkgData: res, wg: &sync.WaitGroup{}, err: nil}
	wrtReq.wg.Add(1)
	c.writeReqs <- &wrtReq
	wrtReq.wg.Wait()

	return &res.Update.Merkle, wrtReq.err
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
	close(c.activations)
	close(c.compReqs)
	close(c.writeReqs)
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
