// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sync"
	"time"

	"amber/source"
)

type BlobRepo struct {
	Source   source.Source
	Address  string
	Interval time.Duration
}

func FetchBlob(repos []BlobRepo, blob string, muRun *sync.Mutex, outputDir string) error {
	muRun.Lock()
	defer muRun.Unlock()

	for i := range repos {
		reader, sz, err := FetchBlobFromRepo(repos[i], blob)
		if err != nil {
			log.Printf("Got error trying to get blob\n")
			continue
		}
		err = WriteBlob(filepath.Join(outputDir, blob), sz, reader)
		reader.Close()
		if err == nil {
			return nil
		}
	}

	return fmt.Errorf("couldn't fetch blob %q from any repo", blob)
}

// FetchBlobFromRepo attempts to pull the set of blobs requested from the supplied
// BlobRepo. FetchBlob returns the list of blobs successfully stored.
func FetchBlobFromRepo(r BlobRepo, blob string) (io.ReadCloser, int64, error) {
	var client *http.Client
	if r.Source != nil {
		client = r.Source.GetHttpClient()
	}
	if client == nil {
		client = http.DefaultClient
	}

	u, err := url.Parse(r.Address)
	if err != nil {
		return nil, -1, err
	}

	tmp := *u
	tmp.Path = filepath.Join(u.Path, blob)
	srcAddr, err := url.Parse(tmp.String())

	if err != nil {
		return nil, -1, err
	}

	resp, err := client.Get(srcAddr.String())
	if err != nil {
		return nil, -1, err
	}

	if resp.StatusCode == 200 {
		return resp.Body, resp.ContentLength, nil
	} else {
		resp.Body.Close()
		return nil, -1, fmt.Errorf("fetch failed with status %s", resp.StatusCode)
	}
}

func WriteBlob(name string, sz int64, con io.ReadCloser) error {
	f, err := os.Create(name)
	if err != nil {
		return err
	}
	defer f.Close()

	err = f.Truncate(sz)
	if err != nil {
		return err
	}

	_, err = io.Copy(f, con)
	return err
}
