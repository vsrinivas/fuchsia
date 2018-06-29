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

var muMap = &sync.Mutex{}
var requestMap = map[string]*fetchJob{}

type fetchJob struct {
	cond *sync.Cond
	err  error
}

func FetchBlob(repos []BlobRepo, blob string, outputDir string) error {
	// if a fetch is already happening, just wait for the result
	// from that
	muMap.Lock()
	job, ok := requestMap[blob]
	// fetch not already in progress, create a job record
	if !ok {
		job = &fetchJob{cond: sync.NewCond(&sync.Mutex{}), err: nil}
		requestMap[blob] = job
		defer func() {
			muMap.Lock()
			delete(requestMap, blob)
			job.cond.Broadcast()
			muMap.Unlock()
		}()
	}
	muMap.Unlock()

	// someone else is already doing a fetch, wait for their result
	if ok {
		job.cond.L.Lock()
		job.cond.Wait()
		job.cond.L.Unlock()
		return job.err
	}

	// if we go this far, the fetch is our job
	var err error
	for i := range repos {
		reader, sz, err2 := FetchBlobFromRepo(repos[i], blob)
		if err2 != nil {
			log.Printf("got error trying to get blob: %s", err2)
			continue
		}
		err = WriteBlob(filepath.Join(outputDir, blob), sz, reader)
		reader.Close()
		// if the blob exists, someone beat us to it
		if err == nil || os.IsExist(err) {
			return nil
		}
	}

	if err != nil {
		job.err = fmt.Errorf("attempted write of %q failed: %s", blob, err)
	} else {
		job.err = fmt.Errorf("couldn't fetch blob %q from any repo", blob)
	}
	return job.err
}

// FetchBlobFromRepo starts an IO request to the given repository and returns
// the io.ReadCloser, size and any error enountered.
func FetchBlobFromRepo(r BlobRepo, blob string) (io.ReadCloser, int64, error) {
	var client *http.Client
	var err error
	if r.Source != nil {
		client, err = r.Source.GetHttpClient()
		if err != nil {
			return nil, -1, err
		}
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
	f, err := os.OpenFile(name, os.O_CREATE|os.O_WRONLY, os.ModePerm)
	if err != nil {
		return err
	}
	defer f.Close()

	err = f.Truncate(sz)
	if err != nil {
		return err
	}

	written, err := io.Copy(f, con)
	if err != nil {
		return err
	}

	if written != sz {
		return fmt.Errorf("blob incomplete, only wrote %d out of %d bytes", written, sz)
	}

	return nil
}
