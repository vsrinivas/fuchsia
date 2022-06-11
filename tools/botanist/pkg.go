// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"context"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"net/http"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"strings"

	"cloud.google.com/go/storage"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pmhttp"
	"go.fuchsia.dev/fuchsia/tools/lib/gcsutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	repoID               = "fuchsia-pkg://fuchsia.com"
	localhostPlaceholder = "localhost"
	DefaultPkgSrvPort    = 8083
)

var (
	// getGCSReader allows us to stub out the GCS communiciation in the tests.
	getGCSReader = getGCSReaderImpl
)

func getGCSReaderImpl(ctx context.Context, client *storage.Client, bucket, path string) (io.ReadCloser, error) {
	bkt := client.Bucket(bucket)
	obj := bkt.Object(path)
	return gcsutil.NewObjectReader(ctx, obj)
}

// cachedPkgRepo is a custom HTTP handler that acts as a GCS redirector with a
// local filesystem cache.
type cachedPkgRepo struct {
	loggerCtx context.Context
	gcsClient *storage.Client
	repoPath  string
	repoURL   *url.URL
	blobURL   *url.URL
}

func newCachedPkgRepo(ctx context.Context, repoPath, repoURL, blobURL string) (*cachedPkgRepo, error) {
	client, err := storage.NewClient(ctx)
	if err != nil {
		return nil, err
	}
	rURL, err := url.Parse(repoURL)
	if err != nil {
		return nil, err
	}
	bURL, err := url.Parse(blobURL)
	if err != nil {
		return nil, err
	}
	return &cachedPkgRepo{
		loggerCtx: ctx,
		repoPath:  repoPath,
		gcsClient: client,
		repoURL:   rURL,
		blobURL:   bURL,
	}, nil
}

func (c *cachedPkgRepo) logf(msg string, args ...interface{}) {
	logger.Debugf(c.loggerCtx, fmt.Sprintf("[package server] %s", msg), args)
}

func (c *cachedPkgRepo) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	localPath := path.Join(c.repoPath, r.URL.Path)
	if _, err := os.Stat(localPath); err != nil {
		// If the requested path does not exist locally, fetch it from GCS.
		if errors.Is(err, os.ErrNotExist) {
			statusCode, err := c.fetchFromGCS(r.Context(), r.URL, localPath)
			if err != nil {
				c.logf("failed to fetch %s from GCS: %s", r.URL.Path, err)
				w.WriteHeader(statusCode)
				return
			}
		} else {
			c.logf("failed to stat %s: %s", localPath, err)
			w.WriteHeader(http.StatusInternalServerError)
			return
		}
	}
	contents, err := ioutil.ReadFile(localPath)
	if err != nil {
		c.logf("failed to read file %s: %s", localPath, err)
		w.WriteHeader(http.StatusInternalServerError)
		return
	}

	// The package resolver requires the Content-Length header.
	w.Header().Set("Content-Length", strconv.Itoa(len(contents)))
	w.WriteHeader(http.StatusOK)
	w.Write(contents)
}

func (c *cachedPkgRepo) fetchFromGCS(ctx context.Context, resource *url.URL, localPath string) (int, error) {
	var bucket string
	if strings.HasPrefix(resource.Path, "/repository") {
		bucket = c.repoURL.Host
	} else if strings.HasPrefix(resource.Path, "/blobs") {
		bucket = c.blobURL.Host
	} else {
		return http.StatusBadRequest, fmt.Errorf("unsupported path: %s", resource.Path)
	}

	resourcePath := strings.TrimLeft(resource.Path, "/")
	r, err := getGCSReader(ctx, c.gcsClient, bucket, resourcePath)
	if err != nil {
		if errors.Is(err, storage.ErrObjectNotExist) {
			return http.StatusNotFound, err
		}
		return http.StatusInternalServerError, err
	}
	defer r.Close()

	w, err := os.Create(localPath)
	if err != nil {
		return http.StatusInternalServerError, err
	}
	defer w.Close()

	if _, err := io.Copy(w, r); err != nil {
		return http.StatusInternalServerError, err
	}
	return http.StatusOK, nil
}

// NewPackageServer creates and starts a local package server.
func NewPackageServer(ctx context.Context, repoPath, remoteRepoURL, remoteBlobURL string, port int) (string, string, error) {
	logger.Debugf(ctx, "creating package server serving from %s", repoPath)

	// Create HTTP handlers for the package server.
	rootJsonBytes, err := ioutil.ReadFile(filepath.Join(repoPath, "repository", "root.json"))
	if err != nil {
		return "", "", err
	}
	cs := pmhttp.NewConfigServerV2(func() []byte {
		return rootJsonBytes
	}, false)
	cPkgRepo, err := newCachedPkgRepo(ctx, repoPath, remoteRepoURL, remoteBlobURL)
	if err != nil {
		return "", "", err
	}

	// Register the handlers and create the server.
	mux := http.NewServeMux()
	mux.Handle("/config.json", cs)
	mux.Handle("/", cPkgRepo)
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
	blobURL := fmt.Sprintf("http://%s:%d/blobs", localhostPlaceholder, port)
	return repoURL, blobURL, nil
}
