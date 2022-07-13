// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"context"
	"encoding/json"
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
	"time"

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

// downloadRecord contains a path we fetched from the remote and how much data
// we retrieved.
type downloadRecord struct {
	// Path is the path requested by the package resolver.
	Path string `json:"path"`

	// Size is the size of the blob.
	Size int `json:"size"`
}

func getGCSReaderImpl(ctx context.Context, client *storage.Client, bucket, path string) (io.ReadCloser, error) {
	bkt := client.Bucket(bucket)
	obj := bkt.Object(path)
	return gcsutil.NewObjectReader(ctx, obj)
}

// cachedPkgRepo is a custom HTTP handler that acts as a GCS redirector with a
// local filesystem cache.
type cachedPkgRepo struct {
	loggerCtx        context.Context
	downloadManifest []downloadRecord
	gcsClient        *storage.Client
	fileServer       http.Handler
	repoPath         string
	repoURL          *url.URL
	blobURL          *url.URL

	totalBytesServed  int
	serveTimeSec      float64
	totalBytesFetched int
	gcsFetchTimeSec   float64
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
		loggerCtx:  ctx,
		fileServer: http.FileServer(http.Dir(repoPath)),
		repoPath:   repoPath,
		gcsClient:  client,
		repoURL:    rURL,
		blobURL:    bURL,
	}, nil
}

func (c *cachedPkgRepo) logf(msg string, args ...interface{}) {
	logger.Debugf(c.loggerCtx, fmt.Sprintf("[package server] %s", msg), args)
}

func (c *cachedPkgRepo) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	startTime := time.Now()
	localPath := path.Join(c.repoPath, r.URL.Path)

	// If the requested path does not exist locally, fetch it from GCS.
	if _, err := os.Stat(localPath); err != nil {
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

	// Once we've asserted that the blob exists locally, redirect the request
	// to the file server.
	c.fileServer.ServeHTTP(w, r)

	// Update the serving metrics.
	if length := w.Header().Get("Content-Length"); length != "" {
		l, err := strconv.Atoi(length)
		if err != nil {
			c.logf("failed to convert content length %s to integer: %s", length, err)
		} else {
			c.totalBytesServed += l
		}
	}
	c.serveTimeSec += time.Since(startTime).Seconds()
}

func (c *cachedPkgRepo) fetchFromGCS(ctx context.Context, resource *url.URL, localPath string) (int, error) {
	startTime := time.Now()
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

	size, err := io.Copy(w, r)
	if err != nil {
		return http.StatusInternalServerError, err
	}

	// Update metrics and download records.
	dr := downloadRecord{
		Path: resourcePath,
		Size: int(size),
	}
	c.downloadManifest = append(c.downloadManifest, dr)
	c.gcsFetchTimeSec += time.Since(startTime).Seconds()
	c.totalBytesFetched += int(size)
	return http.StatusOK, nil
}

func (c *cachedPkgRepo) writeDownloadManifest(path string) error {
	b, err := json.Marshal(c.downloadManifest)
	if err != nil {
		return err
	}
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = f.Write(b)
	return err
}

// PackageServer is the HTTP server that serves packages.
type PackageServer struct {
	loggerCtx            context.Context
	srv                  *http.Server
	c                    *cachedPkgRepo
	downloadManifestPath string
	RepoURL              string
	BlobURL              string
}

func (p *PackageServer) Close() error {
	logger.Debugf(p.loggerCtx, "stopping package server")
	if p.downloadManifestPath != "" {
		p.c.writeDownloadManifest(p.downloadManifestPath)
	}
	logger.Debugf(p.loggerCtx, "----------------------------------------------------")
	logger.Debugf(p.loggerCtx, "Package server data")
	logger.Debugf(p.loggerCtx, "----------------------------------------------------")
	logger.Debugf(p.loggerCtx, "Total data served (bytes): %d", p.c.totalBytesServed)
	logger.Debugf(p.loggerCtx, "Total time spent serving (seconds): %f", p.c.serveTimeSec)
	logger.Debugf(p.loggerCtx, "Total data fetched from GCS (bytes): %d", p.c.totalBytesFetched)
	logger.Debugf(p.loggerCtx, "Total time spent downloading from GCS (seconds): %f", p.c.gcsFetchTimeSec)
	logger.Debugf(p.loggerCtx, "----------------------------------------------------")
	return p.srv.Close()
}

// NewPackageServer creates and starts a local package server.
func NewPackageServer(ctx context.Context, repoPath, remoteRepoURL, remoteBlobURL, downloadManifestPath string, port int) (*PackageServer, error) {
	logger.Debugf(ctx, "creating package server serving from %s", repoPath)

	// Create HTTP handlers for the package server.
	rootJsonBytes, err := ioutil.ReadFile(filepath.Join(repoPath, "repository", "root.json"))
	if err != nil {
		return nil, err
	}
	cs := pmhttp.NewConfigServerV2(func() []byte {
		return rootJsonBytes
	}, false)
	cPkgRepo, err := newCachedPkgRepo(ctx, repoPath, remoteRepoURL, remoteBlobURL)
	if err != nil {
		return nil, err
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
		addr := fmt.Sprintf("0.0.0.0:%d", port)
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

	// Do not return until the package server has actually started serving.
	<-pkgSrvStarted
	logger.Debugf(ctx, "package server started")
	return &PackageServer{
		loggerCtx:            ctx,
		srv:                  pkgSrv,
		c:                    cPkgRepo,
		downloadManifestPath: downloadManifestPath,
		RepoURL:              fmt.Sprintf("http://%s:%d/repository", localhostPlaceholder, port),
		BlobURL:              fmt.Sprintf("http://%s:%d/blobs", localhostPlaceholder, port),
	}, nil
}
