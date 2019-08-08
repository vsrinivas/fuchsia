// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package control_server

import (
	"fmt"
	"log"
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"

	"amber/daemon"
	"amber/source"

	"fidl/fuchsia/amber"
	"fidl/fuchsia/pkg"
)

type ControlServer struct {
	*amber.ControlTransitionalBase
	daemon    *daemon.Daemon
	openRepos amber.OpenedRepositoryService
}

var _ = amber.Control((*ControlServer)(nil))

func logFailure(msg string) error {
	log.Printf(msg)
	// Return nil to FIDL clients so the channel is not closed.  All moved/obsolete APIs are essentially no-ops.
	return nil
}

func moved(from, to string) error {
	return logFailure(fmt.Sprintf("%s moved to %s", from, to))
}

func obsolete(name string) error {
	return logFailure(fmt.Sprintf("%s no longer supported", name))
}

func NewControlServer(d *daemon.Daemon) *ControlServer {
	return &ControlServer{
		daemon: d,
	}
}

func (c *ControlServer) DoTest(in int32) (out string, err error) {
	r := fmt.Sprintf("Your number was %d\n", in)
	return r, nil
}

func (c *ControlServer) AddSrc(cfg amber.SourceConfig) (bool, error) {
	return true, moved("AddSrc", "fuchsia.pkg.RepositoryManager")
}

func (c *ControlServer) CheckForSystemUpdate() (bool, error) {
	return false, moved("CheckForSystemUpdate", "fuchsia.update.Manager")
}

func (c *ControlServer) RemoveSrc(id string) (amber.Status, error) {
	return amber.StatusOk, moved("RemoveSrc", "fuchsia.pkg.RepositoryManager")
}

func (c *ControlServer) ListSrcs() ([]amber.SourceConfig, error) {
	return nil, moved("ListSrcs", "fuchsia.pkg.RepositoryManager")
}

func (c *ControlServer) GetUpdateComplete(name string, ver, mer *string) (zx.Channel, error) {
	return zx.Channel(zx.HandleInvalid), moved("GetUpdateComplete", "fuchsia.pkg.PackageResolver")
}

func (c *ControlServer) PackagesActivated(merkle []string) error {
	return obsolete("PackagesActivated")
}

func (c *ControlServer) PackagesFailed(merkle []string, status int32, blobMerkle string) error {
	return obsolete("PackagesFailed")
}

func (c *ControlServer) SetSrcEnabled(id string, enabled bool) (amber.Status, error) {
	return amber.StatusErr, moved("SetSrcEnabled", "fuchsia.pkg.rewrite.Engine")
}

func (c *ControlServer) GetBlob(merkle string) error {
	return obsolete("GetBlob")
}

func (c *ControlServer) Login(srcId string) (*amber.DeviceCode, error) {
	return nil, obsolete("Login")
}

func (c *ControlServer) Gc() error {
	return c.daemon.GC()
}

type repoHandler struct {
	config              pkg.RepositoryConfig
	repo                source.Repository
	outstandingRequests sync.WaitGroup
}

func (h *repoHandler) GetUpdateComplete(name string, variant *string, merkle *string, result amber.FetchResultInterfaceRequest) error {
	_, _ = variant, merkle
	log.Printf("getting update for %s from %s", name, h.config.RepoUrl)
	resultChannel := fidl.InterfaceRequest(result).Channel
	resultProxy := (*amber.FetchResultEventProxy)(&fidl.ChannelProxy{Channel: resultChannel})
	h.outstandingRequests.Add(1)

	go func() {
		defer h.outstandingRequests.Done()
		defer resultProxy.Close()
		result, status, err := h.repo.GetUpdateComplete(name, variant, merkle)
		if err != nil {
			err := resultProxy.OnError((int32)(status), err.Error())
			if err != nil {
				// Ignore errors here, it just means whoever asked for this has gone away already.
				log.Printf("can't report error for update of %s; caller didn't care enough to stick around.", name)
			}
		} else {
			err := resultProxy.OnSuccess(result)
			if err != nil {
				// Ignore errors here, it just means whoever asked for this has gone away already.
				log.Printf("can't report success for update of %s; caller didn't care enough to stick around.", name)
			}
		}
	}()

	return nil
}

func (h *repoHandler) MerkleFor(name string, variant *string) (status int32, message string, merkle string, size int64, fatalErr error) {
	log.Printf("MerkleFor %s from %s", name, h.config.RepoUrl)
	var variantStr string
	if variant != nil {
		variantStr = *variant
	}

	merkle, size, err := h.repo.MerkleFor(name, variantStr, "")
	if err != nil {
		return (int32)(zx.ErrInternal), err.Error(), "", 0, nil
	}
	return (int32)(zx.ErrOk), "", merkle, size, nil
}

func (h *repoHandler) Close() error {
	go func() {
		h.outstandingRequests.Wait()
		h.repo.Close()
	}()
	return nil
}

var _ amber.OpenedRepository = (*repoHandler)(nil)

func (c *ControlServer) OpenRepository(config pkg.RepositoryConfig, repo amber.OpenedRepositoryInterfaceRequest) (int32, error) {
	log.Printf("opening repository: %q", config.RepoUrl)
	opened, err := c.daemon.OpenRepository(&config)
	if err != nil {
		log.Printf("error opening repository %q: %v", config.RepoUrl, err)
		repo.Close()
		return (int32)(zx.ErrInternal), nil
	}
	handler := &repoHandler{config, opened, sync.WaitGroup{}}
	c.openRepos.Add(handler, (fidl.InterfaceRequest(repo)).Channel, func(err error) {
		log.Printf("closing repository: %s", config.RepoUrl)
		handler.Close()
	})
	return (int32)(zx.ErrOk), nil
}
