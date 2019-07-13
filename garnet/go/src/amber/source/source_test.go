// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"fidl/fuchsia/amber"
	"fidl/fuchsia/pkg"
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"os"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"fuchsia.googlesource.com/sse"
)

func TestAutoWatch(t *testing.T) {
	var updateWaiter sync.WaitGroup
	var oldUpdate func(*Source) error
	oldUpdate, update = update, func(*Source) error {
		updateWaiter.Done()
		return nil
	}
	defer func() {
		update = oldUpdate
	}()

	// holds the responsewriter from the server after sse start
	var serverVal atomic.Value
	var serverStop sync.WaitGroup
	serverStop.Add(1)
	srv := httptest.NewServer(http.HandlerFunc(func(wr http.ResponseWriter, r *http.Request) {
		sse.Start(wr, r)
		serverVal.Store(wr)
		serverStop.Wait()
	}))
	defer srv.Close()
	defer srv.CloseClientConnections()

	d, err := ioutil.TempDir("", "amber-test-autowatch")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(d)

	src, err := NewSource(d, &amber.SourceConfig{
		Id: "test-source",
		RootKeys: []amber.KeyConfig{
			{
				Type:  "ed25519",
				Value: "c32663b84cb8ffa39b436096b535133524bf00a863b35722b7c0e33d061af5cf",
			},
		},
		Auto:    true,
		RepoUrl: srv.URL,
	})
	if err != nil {
		panic(err)
	}
	src.httpClient = srv.Client()
	src.SetEnabled(true)

	var w sync.WaitGroup
	w.Add(1)
	go func() {
		defer w.Done()
		src.watch()
	}()

	var serverWriter http.ResponseWriter
	for {
		v := serverVal.Load()
		if v != nil {
			serverWriter = v.(http.ResponseWriter)
			break
		}
		time.Sleep(time.Second)
	}

	for i := 1; i < 3; i++ {
		updateWaiter.Add(1)
		if err := sse.Write(serverWriter, &sse.Event{}); err != nil {
			panic(err)
		}
	}
	// We've observed each of the update events that we sent
	updateWaiter.Wait()

	// Once the source is disabled, closing the server end should cause the
	// autowatch loop to exit. If the autoloop does not exit, the test will hang in
	// the waitgroup "w" to timeout.
	src.SetEnabled(false)

	// Let the server shutdown the connections to the client
	serverStop.Done()
	w.Wait()
}

func TestConvertRepositoryConfig(t *testing.T) {
	config := &pkg.RepositoryConfig{
		Mirrors: []pkg.MirrorConfig{
			{
				MirrorUrl:        "http://example.com:8083/",
				MirrorUrlPresent: true,
				Subscribe:        true,
				SubscribePresent: true,
			},
		},
	}
	result, err := convertRepositoryConfig(config)
	if err != nil {
		t.Fatal(err)
	}

	if result.RepoUrl != "http://example.com:8083/" {
		t.Fatalf("bad RepoUrl; want %q got %q", "http://example.com:8083/", result.RepoUrl)
	}
	if result.StatusConfig.Enabled == false {
		t.Fatal("resulting source config should be enabled.")
	}
	if result.Auto == false {
		t.Fatal("resulting source config should have auto enabled.")
	}
	if result.RatePeriod != 60 {
		t.Fatalf("bad RatePeriod; want 60 got %v", result.RatePeriod)
	}
}

func TestConvertRepositoryConfig_noSubscribe(t *testing.T) {
	config := &pkg.RepositoryConfig{
		Mirrors: []pkg.MirrorConfig{
			{
				MirrorUrl:        "http://example.com:8083/",
				MirrorUrlPresent: true,
				Subscribe:        false,
				SubscribePresent: true,
			},
		},
	}
	result, err := convertRepositoryConfig(config)
	if err != nil {
		t.Fatal(err)
	}

	if result.Auto == true {
		t.Fatal("resulting source config should not have auto enabled.")
	}
	if result.RatePeriod != 0 {
		t.Fatalf("bad RatePeriod; want 0 got %v", result.RatePeriod)
	}
}
