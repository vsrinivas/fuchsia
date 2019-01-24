// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package amber

import (
	"context"
	"fmt"
	"net/http"
	"path"
)

// DefaultServerAddress is the default address to serve packages from.
const DefaultServerAddress = "localhost:8083"

// DefaultPackagesPath returns the default path to an amber package repository within a
// Fuchsia build directory.
func DefaultPackagesPath(buildDir string) string {
	return path.Join(buildDir, "amber-files", "repository")
}

// Middleware creates a new http.Handler that executes itself before executing next. The
// new handler should always call next to give the server a chance to serve files.
type Middleware func(next http.Handler) http.Handler

// ServerOptions specify how to serve a package repository.
type ServerOptions struct {
	// Address is the HTTP address to serve from
	Address string

	// PackagesPath is the path to the package repository to serve.
	PackagesPath string

	// Middleware is optional HTTP middleware to use for the Server. This will be called
	// on every incoming request before the server handles the message.
	Middleware Middleware
}

// ServePackages starts an OTA server for the amber repository located at path. Address
// is the HTTP address to serve from. This function will block until the server is done.
// The server will exit when the provide context is done. Returns the string URL of the
// package server.
func ServePackages(ctx context.Context, opts ServerOptions) (url string) {
	handler := http.FileServer(http.Dir(opts.PackagesPath))
	if opts.Middleware != nil {
		handler = opts.Middleware(handler)
	}

	s := &server{
		address: opts.Address,
		handler: handler,
	}

	s.delegate = &http.Server{
		Addr:    opts.Address,
		Handler: s.handler,
	}

	go s.start(ctx)
	return s.url()
}

// server is a handle to a package repository server.
type server struct {
	address  string
	delegate *http.Server
	handler  http.Handler
}

func (s *server) url() string {
	return fmt.Sprintf("http://%s", s.address)
}

func (s *server) start(ctx context.Context) error {
	errors := make(chan error)

	go func() {
		errors <- s.delegate.ListenAndServe()
	}()

	select {
	case <-ctx.Done():
		if err := s.delegate.Close(); err != nil {
			return err
		}
		return ctx.Err()
	case err := <-errors:
		return err
	}
}
