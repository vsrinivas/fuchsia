// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package control_server

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"syscall/zx"

	"amber/daemon"
	"amber/metrics"
	"amber/sys_update"

	"fidl/fuchsia/amber"
)

type ControlServer struct {
	*amber.ControlTransitionalBase
	daemon    *daemon.Daemon
	sysUpdate *sys_update.SystemUpdateMonitor
}

var _ = amber.Control((*ControlServer)(nil))

type EventsImpl struct{}

var _ = amber.Events(EventsImpl{})

var merklePat = regexp.MustCompile("^[0-9a-f]{64}$")

func NewControlServer(d *daemon.Daemon, sum *sys_update.SystemUpdateMonitor) *ControlServer {
	return &ControlServer{
		daemon:    d,
		sysUpdate: sum,
	}
}

func (c *ControlServer) DoTest(in int32) (out string, err error) {
	r := fmt.Sprintf("Your number was %d\n", in)
	return r, nil
}

func (c *ControlServer) AddSrc(cfg amber.SourceConfig) (bool, error) {
	if err := c.daemon.AddSource(&cfg); err != nil {
		log.Printf("error adding source: %s", err)
		return false, nil
	}

	return true, nil
}

func (c *ControlServer) CheckForSystemUpdate() (bool, error) {
	go c.sysUpdate.Check(metrics.InitiatorManual)
	return true, nil
}

func (c *ControlServer) RemoveSrc(id string) (amber.Status, error) {
	return c.daemon.RemoveSource(id)
}

func (c *ControlServer) ListSrcs() ([]amber.SourceConfig, error) {
	m := c.daemon.GetSources()
	v := make([]amber.SourceConfig, 0, len(m))
	for _, src := range m {
		c := *src.GetConfig()
		c.StatusConfig.Enabled = src.Enabled()
		v = append(v, c)
	}

	return v, nil
}

func (c *ControlServer) GetUpdateComplete(name string, ver, mer *string) (zx.Channel, error) {
	r, ch, e := zx.NewChannel(0)
	if e != nil {
		log.Printf("getupdatecomplete: could not create channel")
		// TODO(raggi): the client is just going to get peer closed, and no indication of why
		return zx.Channel(zx.HandleInvalid), nil
	}

	if len(name) == 0 {
		log.Printf("getupdatecomplete: invalid arguments: empty name")
		ch.Handle().SignalPeer(0, zx.SignalUser0)
		ch.Write([]byte(zx.ErrInvalidArgs.String()), []zx.Handle{}, 0)
		ch.Close()
		return r, nil
	}

	var (
		version string
		merkle  string
	)

	if ver != nil {
		version = *ver
	}
	if mer != nil {
		merkle = *mer
	}

	go func() {
		defer ch.Close()
		c.daemon.UpdateIfStale()

		root, length, err := c.daemon.MerkleFor(name, version, merkle)
		if err != nil {
			log.Printf("control_server: could not get update for %s: %s", filepath.Join(name, version, merkle), err)
			ch.Handle().SignalPeer(0, zx.SignalUser0)
			ch.Write([]byte(err.Error()), []zx.Handle{}, 0)
			return
		}

		if _, err := os.Stat(filepath.Join("/pkgfs/versions", root)); err == nil {
			ch.Write([]byte(root), []zx.Handle{}, 0)
			return
		}

		log.Printf("control_server: get update: %s", filepath.Join(name, version, merkle))

		err = c.daemon.GetPkg(root, length)
		if os.IsExist(err) {
			log.Printf("control_server: %s already installed", filepath.Join(name, version, root))
			// signal success to the client
			err = nil
		}
		if err != nil {
			log.Printf("control_server: error downloading package: %s", err)
			ch.Handle().SignalPeer(0, zx.SignalUser0)
			ch.Write([]byte(err.Error()), []zx.Handle{}, 0)
		} else {
			ch.Write([]byte(root), []zx.Handle{}, 0)
		}
	}()
	return r, nil
}

func (c *ControlServer) PackagesActivated(merkle []string) error {
	return nil
}

func (c *ControlServer) PackagesFailed(merkle []string, status int32, blobMerkle string) error {
	return nil
}

func (c *ControlServer) SetSrcEnabled(id string, enabled bool) (amber.Status, error) {
	var err error
	if enabled {
		err = c.daemon.EnableSource(id)
	} else {
		err = c.daemon.DisableSource(id)
	}

	if err != nil {
		log.Printf("control_server: ERROR: SetSrcEnabled(%s, %v) -> %v", id, enabled, err)
		return amber.StatusErr, nil
	}

	return amber.StatusOk, nil
}

func (c *ControlServer) GetBlob(merkle string) error {
	return nil
}

func (c *ControlServer) Login(srcId string) (*amber.DeviceCode, error) {
	return nil, nil
}

func (c *ControlServer) Gc() error {
	return c.daemon.GC()
}
