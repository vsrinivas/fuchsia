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
)

type BlobRepo struct {
	Address  string
	Interval time.Duration
}

func FetchBlob(repos []BlobRepo, blob string, muRun *sync.Mutex, outputDir string) error {
	muRun.Lock()
	defer muRun.Unlock()

	httpC := &http.Client{}

	for i, _ := range repos {
		reader, sz, err := FetchBlobFromRepo(repos[i], blob, httpC)
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

// FetchBlob attempts to pull the set of blobs requested from the supplied
// BlobRepo. FetchBlob returns the list of blobs successfully stored.
func FetchBlobFromRepo(r BlobRepo, blob string, client *http.Client) (io.ReadCloser, int64, error) {
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

	if r, err := client.Get(srcAddr.String()); err == nil {
		if r.StatusCode == 200 {
			return r.Body, r.ContentLength, nil
		} else {
			r.Body.Close()
			return nil, -1, fmt.Errorf("fetch failed with status %s", r.StatusCode)
		}
	} else {
		return nil, -1, err
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
