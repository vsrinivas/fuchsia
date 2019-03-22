// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package amber

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	tuf_data "github.com/flynn/go-tuf/data"
)

type loggingWriter struct {
	http.ResponseWriter
	status int
}

func (lw *loggingWriter) WriteHeader(status int) {
	lw.status = status
	lw.ResponseWriter.WriteHeader(status)
}

// ServeRepository serves the amber repository in an http server.
func ServeRepository(t *testing.T, repoDir string) (int, error) {
	http.Handle("/", http.FileServer(http.Dir(repoDir)))

	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		return 0, err
	}
	port := listener.Addr().(*net.TCPAddr).Port
	log.Printf("Serving %s on :%d", repoDir, port)

	go http.Serve(listener, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		lw := &loggingWriter{w, 0}
		http.DefaultServeMux.ServeHTTP(lw, r)
		log.Printf("%s [pm serve] %d %s\n",
			time.Now().Format("2006-01-02 15:04:05"), lw.status, r.RequestURI)
	}))

	return port, nil
}

type keyConfig struct {
	Type  string
	Value string
}

type statusConfig struct {
	Enabled bool
}

type sourceConfig struct {
	ID           string       `json:"id"`
	RepoURL      string       `json:"repoUrl"`
	BlobRepoURL  string       `json:"blobRepoUrl"`
	RootKeys     []keyConfig  `json:"rootKeys"`
	StatusConfig statusConfig `json:"statusConfig"`
}

// WriteConfig writes the source config to the repository.
func WriteConfig(repoDir string, localHostname string, port int) (configURL string, configHash string, err error) {
	f, err := os.Open(filepath.Join(repoDir, "root.json"))
	if err != nil {
		return "", "", err
	}
	defer f.Close()

	var signed tuf_data.Signed
	if err := json.NewDecoder(f).Decode(&signed); err != nil {
		return "", "", err
	}

	var root tuf_data.Root
	if err := json.Unmarshal(signed.Signed, &root); err != nil {
		return "", "", err
	}

	var rootKeys []keyConfig
	for _, keyID := range root.Roles["root"].KeyIDs {
		key := root.Keys[keyID]

		rootKeys = append(rootKeys, keyConfig{
			Type:  key.Type,
			Value: key.Value.Public.String(),
		})
	}

	hostname := strings.SplitN(localHostname, "%", 2)[0]
	repoURL := fmt.Sprintf("http://[%s]:%d", hostname, port)
	configURL = fmt.Sprintf("%s/system_ota_tests/config.json", repoURL)

	config, err := json.Marshal(&sourceConfig{
		ID:          "system_ota_tests",
		RepoURL:     repoURL,
		BlobRepoURL: fmt.Sprintf("%s/blobs", repoURL),
		RootKeys:    rootKeys,
		StatusConfig: statusConfig{
			Enabled: true,
		},
	})
	if err != nil {
		return "", "", err
	}
	h := sha256.Sum256(config)
	configHash = hex.EncodeToString(h[:])

	configDir := filepath.Join(repoDir, "system_ota_tests")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		return "", "", err
	}

	configPath := filepath.Join(configDir, "config.json")
	log.Printf("writing %q", configPath)
	if err := ioutil.WriteFile(configPath, config, 0644); err != nil {
		return "", "", err
	}

	return configURL, configHash, nil
}
