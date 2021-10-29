// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pmhttp"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/lib/repo"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
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

// AddPackageRepository adds a package repository to a connected fuchsia
// instance with the provided metadata and blob URLs.
// In either URL, a host of "localhost" will be resolved and scoped as
// appropriate when dealing with the address from the host and target
// perspectives.
func AddPackageRepository(ctx context.Context, client *sshutil.Client, repoURL, blobURL string) error {
	localhost := strings.Contains(repoURL, localhostPlaceholder) || strings.Contains(blobURL, localhostPlaceholder)

	lScopedRepoURL := repoURL
	if localhost {
		host := localScopedLocalHost(client.LocalAddr().String())
		lScopedRepoURL = strings.Replace(repoURL, localhostPlaceholder, host, 1)
		logger.Infof(ctx, "local-scoped package repository address: %s\n", lScopedRepoURL)
	}

	rScopedRepoURL := repoURL
	rScopedBlobURL := blobURL
	if localhost {
		host, err := remoteScopedLocalHost(ctx, client)
		if err != nil {
			return err
		}
		rScopedRepoURL = strings.Replace(repoURL, localhostPlaceholder, host, 1)
		logger.Infof(ctx, "remote-scoped package repository address: %s\n", rScopedRepoURL)
		rScopedBlobURL = strings.Replace(blobURL, localhostPlaceholder, host, 1)
		logger.Infof(ctx, "remote-scoped package blob address: %s\n", rScopedBlobURL)
	}

	rootMeta, err := repo.GetRootMetadataInsecurely(ctx, lScopedRepoURL)
	if err != nil {
		return fmt.Errorf("failed to derive root metadata: %w", err)
	}

	cfg := &repo.Config{
		URL:           repoID,
		RootKeys:      rootMeta.RootKeys,
		RootVersion:   rootMeta.RootVersion,
		RootThreshold: rootMeta.RootThreshold,
		Mirrors: []repo.MirrorConfig{
			{
				URL:     rScopedRepoURL,
				BlobURL: rScopedBlobURL,
			},
		},
	}

	return repo.AddFromConfig(ctx, client, cfg)
}

func localScopedLocalHost(laddr string) string {
	tokens := strings.Split(laddr, ":")
	host := strings.Join(tokens[:len(tokens)-1], ":") // Strips the port.
	return escapePercentSign(host)
}

func remoteScopedLocalHost(ctx context.Context, client *sshutil.Client) (string, error) {
	// From the ssh man page:
	// "SSH_CONNECTION identifies the client and server ends of the connection.
	// The variable contains four space-separated values: client IP address,
	// client port number, server IP address, and server port number."
	// We wish to obtain the client IP address, as will be scoped from the
	// remote address.
	var stdout bytes.Buffer
	if err := client.Run(ctx, []string{"echo", "${SSH_CONNECTION}"}, &stdout, nil); err != nil {
		return "", fmt.Errorf("failed to derive $SSH_CONNECTION: %w", err)
	}
	val := string(stdout.Bytes())
	tokens := strings.Split(val, " ")
	if len(tokens) != 4 {
		return "", fmt.Errorf("$SSH_CONNECTION should be four space-separated values and not %q", val)
	}
	host := escapePercentSign("[" + tokens[0] + "]")
	return host, nil
}

// From the spec https://tools.ietf.org/html/rfc6874#section-2:
// "%" is always treated as an escape character in a URI, so, according to
// the established URI syntax any occurrences of literal "%" symbols in a
// URI MUST be percent-encoded and represented in the form "%25".
func escapePercentSign(addr string) string {
	if strings.Contains(addr, "%25") {
		return addr
	}
	return strings.Replace(addr, "%", "%25", 1)
}
