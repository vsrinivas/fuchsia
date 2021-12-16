// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"context"
	"errors"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pmhttp"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	repoID               = "fuchsia-pkg://fuchsia.com"
	localhostPlaceholder = "localhost"
	DefaultPkgSrvPort    = 8083
)

// NewPackageServer creates and starts a local package server.
func NewPackageServer(ctx context.Context, repoPath string, port int) (string, string, error) {
	logger.Debugf(ctx, "creating package server serving from %s", repoPath)

	// Create HTTP handlers for the package server.
	rootJsonBytes, err := ioutil.ReadFile(filepath.Join(repoPath, "repository", "root.json"))
	if err != nil {
		return "", "", err
	}
	cs := pmhttp.NewConfigServerV2(func() []byte {
		return rootJsonBytes
	}, false)
	dirServer := http.FileServer(http.Dir(repoPath))

	// Register the handlers and create the server.
	mux := http.NewServeMux()
	mux.Handle("/config.json", cs)
	mux.Handle("/", dirServer)
	pkgSrv := &http.Server{
		Handler: mux,
	}

	// Start the server and spin off a handler to stop it when the context
	// is canceled.
	pkgSrvStarted := make(chan struct{})
	go func() {
		addr := fmt.Sprintf(":%d", port)
		logger.Debugf(ctx, "starting package server on %s", addr)
		l, err := net.Listen("tcp", addr)
		if err != nil {
			logger.Errorf(ctx, "listening on %s failed: %s", addr, err)
		}
		close(pkgSrvStarted)
		if err := pkgSrv.Serve(l); err != nil && !errors.Is(err, http.ErrServerClosed) {
			logger.Errorf(ctx, "package server failed: %s", err)
		}
	}()
	go func() {
		select {
		case <-ctx.Done():
			logger.Debugf(ctx, "stopping package server")
			pkgSrv.Close()
		}
	}()

	// Do not return until the package server has actually started serving.
	<-pkgSrvStarted
	logger.Debugf(ctx, "package server started")
	repoURL := fmt.Sprintf("http://%s:%d/repository", localhostPlaceholder, port)
	blobURL := fmt.Sprintf("%s/blobs", repoURL)
	return repoURL, blobURL, nil
}
