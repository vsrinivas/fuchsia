// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/fs"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	tuf_data "github.com/theupdateframework/go-tuf/data"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/lib/repo"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type Server struct {
	Dir          string
	BlobStore    BlobStore
	URL          string
	Hash         string
	server       *http.Server
	shuttingDown chan struct{}
}

type httpBlobStore struct {
	ctx       context.Context
	blobStore BlobStore
}

func (f httpBlobStore) Open(path string) (fs.File, error) {
	if !strings.HasPrefix(path, "blobs/") {
		return nil, os.ErrNotExist
	}
	return f.blobStore.OpenBlob(f.ctx, strings.TrimPrefix(path, "blobs/"))
}

func newServer(ctx context.Context, dir string, blobStore BlobStore, localHostname string, repoName string) (*Server, error) {
	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		return nil, err
	}

	port := listener.Addr().(*net.TCPAddr).Port
	logger.Infof(ctx, "Serving %s on :%d", dir, port)

	configURL, configHash, config, err := genConfig(dir, localHostname, repoName, port)
	if err != nil {
		listener.Close()
		return nil, err
	}

	mux := http.NewServeMux()
	mux.HandleFunc(fmt.Sprintf("/%s/config.json", repoName), func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		w.Write(config)
	})
	// Blobs requests come as `/blobs/<merkle>` so the directory we actually
	// serve from should be the parent directory of the blobsDir and the blobsDir
	// should be called `blobs`.
	mux.Handle("/blobs/", http.FileServer(http.FS(httpBlobStore{ctx, blobStore})))
	mux.Handle("/", http.FileServer(http.Dir(dir)))

	server := &http.Server{
		Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			lw := &loggingWriter{w, 0}
			mux.ServeHTTP(lw, r)
			logger.Infof(ctx, "%s [repo serve] %d %s\n",
				time.Now().Format("2006-01-02 15:04:05"), lw.status, r.RequestURI)
		}),
	}

	go func() {
		if err := server.Serve(listener); err != http.ErrServerClosed {
			logger.Fatalf(ctx, "failed to shutdown server: %s", err)
		}
	}()

	shuttingDown := make(chan struct{})

	// Tear down the server if the context expires.
	go func() {
		select {
		case <-shuttingDown:
		case <-ctx.Done():
			server.Shutdown(ctx)
		}
	}()

	return &Server{
		Dir:          dir,
		BlobStore:    blobStore,
		URL:          configURL,
		Hash:         configHash,
		server:       server,
		shuttingDown: shuttingDown,
	}, err
}

func (s *Server) Shutdown(ctx context.Context) {
	s.server.Shutdown(ctx)
	close(s.shuttingDown)
}

type loggingWriter struct {
	http.ResponseWriter
	status int
}

func (lw *loggingWriter) WriteHeader(status int) {
	lw.status = status
	lw.ResponseWriter.WriteHeader(status)
}

// writeConfig writes the source config to the repository.
func genConfig(dir string, localHostname string, repoName string, port int) (configURL string, configHash string, config []byte, err error) {
	type sourceConfig struct {
		ID            string
		RepoURL       string
		BlobRepoURL   string
		RatePeriod    int
		RootKeys      []repo.KeyConfig
		RootVersion   uint32 `json:"rootVersion,omitempty"`
		RootThreshold uint32 `json:"rootThreshold,omitempty"`
		StatusConfig  struct {
			Enabled bool
		}
		Auto    bool
		BlobKey *struct {
			Data [32]uint8
		}
	}

	f, err := os.Open(filepath.Join(dir, "root.json"))
	if err != nil {
		return "", "", nil, err
	}
	defer f.Close()

	var signed tuf_data.Signed
	if err := json.NewDecoder(f).Decode(&signed); err != nil {
		return "", "", nil, err
	}

	var root tuf_data.Root
	if err := json.Unmarshal(signed.Signed, &root); err != nil {
		return "", "", nil, err
	}

	rootKeys, err := repo.GetRootKeys(&root)
	if err != nil {
		return "", "", nil, err
	}

	hostname := strings.ReplaceAll(localHostname, "%", "%25")

	var repoURL string
	if strings.Contains(hostname, ":") {
		// This is an IPv6 address, use brackets for an IPv6 literal
		repoURL = fmt.Sprintf("http://[%s]:%d", hostname, port)
	} else {
		repoURL = fmt.Sprintf("http://%s:%d", hostname, port)
	}
	configURL = fmt.Sprintf("%s/%s/config.json", repoURL, repoName)

	var rootThreshold int
	if rootRole, ok := root.Roles["root"]; ok {
		rootThreshold = rootRole.Threshold
	}

	config, err = json.Marshal(&sourceConfig{
		ID:            repoName,
		RepoURL:       repoURL,
		BlobRepoURL:   fmt.Sprintf("%s/blobs", repoURL),
		RatePeriod:    60,
		RootKeys:      rootKeys,
		RootVersion:   uint32(root.Version),
		RootThreshold: uint32(rootThreshold),
		StatusConfig: struct {
			Enabled bool
		}{
			Enabled: true,
		},
		Auto: true,
	})
	if err != nil {
		return "", "", nil, err
	}
	h := sha256.Sum256(config)
	configHash = hex.EncodeToString(h[:])

	return configURL, configHash, config, nil
}
