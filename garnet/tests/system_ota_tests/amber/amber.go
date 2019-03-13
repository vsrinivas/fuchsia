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
func ServeRepository(t *testing.T, repoDir string) {
	log.Printf("Serving %s", repoDir)

	http.Handle("/", http.FileServer(http.Dir(repoDir)))
	err := http.ListenAndServe(":8083", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		lw := &loggingWriter{w, 0}
		http.DefaultServeMux.ServeHTTP(lw, r)
		log.Printf("%s [pm serve] %d %s\n",
			time.Now().Format("2006-01-02 15:04:05"), lw.status, r.RequestURI)
	}))
	if err != nil {
		t.Fatal(err)
	}
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
func WriteConfig(repoDir string, localHostname string) (configURL string, configHash string, err error) {
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
	repoURL := fmt.Sprintf("http://[%s]:8083", hostname)
	configURL = fmt.Sprintf("%s/devhost/config.json", repoURL)

	config, err := json.Marshal(&sourceConfig{
		ID:          "devhost",
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

	configDir := filepath.Join(repoDir, "devhost")
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
