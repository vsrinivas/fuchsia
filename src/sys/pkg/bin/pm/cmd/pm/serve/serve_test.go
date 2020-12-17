// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serve

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/garnet/go/src/sse"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pmhttp"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/repo"
)

// poor mans reset, for flags used in the below tests
func resetFlags() {
	config.RepoDir = ""
	*repoServeDir = ""
	*publishList = ""
	*portFile = ""
}

func resetServer() {
	server = http.Server{}
}

func TestParseFlags(t *testing.T) {
	defer resetFlags()
	if err := ParseFlags([]string{"-repo", "amber-files"}); err != nil {
		t.Fatal(err)
	}
	if got, want := config.RepoDir, "amber-files"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := *repoServeDir, "amber-files/repository"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}

	resetFlags()
	if err := ParseFlags([]string{"-d", "amber-files/repository"}); err != nil {
		t.Fatal(err)
	}
	if got, want := config.RepoDir, "amber-files"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := *repoServeDir, "amber-files/repository"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}

	// TODO: add additional coverage (needs some re-arrangement to be able to
	// re-set flag defaults)
}

func TestServer(t *testing.T) {
	defer resetFlags()
	defer resetServer()
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.BuildTestPackage(cfg)

	portFileDir := t.TempDir()
	portFile := filepath.Join(portFileDir, "port-file")
	repoDir := t.TempDir()
	manifestListPath := filepath.Join(cfg.OutputDir, "pkg-manifests.list")
	pkgManifestPath := filepath.Join(cfg.OutputDir, "package_manifest.json")
	if err := ioutil.WriteFile(manifestListPath, []byte(pkgManifestPath+"\n"), 0o600); err != nil {
		t.Fatal(err)
	}

	repo, err := repo.New(repoDir)
	if err != nil {
		t.Fatal(err)
	}

	if err := repo.Init(); err != nil {
		t.Fatal(err)
	}

	if err := repo.AddTargets([]string{}, json.RawMessage{}); err != nil {
		t.Fatal(err)
	}

	if err := repo.CommitUpdates(false); err != nil {
		t.Fatal(err)
	}

	addrChan := make(chan string)
	var w sync.WaitGroup
	w.Add(1)
	go func() {
		defer w.Done()
		err := Run(cfg, []string{"-l", "127.0.0.1:0", "-repo", repoDir, "-p", manifestListPath, "-f", portFile}, addrChan)
		if err != nil && err != http.ErrServerClosed {
			t.Fatal(err)
		}
	}()
	defer func() {
		server.Close()
		w.Wait()
	}()
	addr := <-addrChan
	baseURL := fmt.Sprintf("http://%s", addr)

	// after building, we need to wait for publication to complete, so start a client for that
	cli := newTestAutoClient(t, baseURL)
	defer cli.close()

	cli.verifyNoPendingEvents()

	// the first event happens as the initial version of the package is published
	event := cli.readEvent()
	if got, want := event.Event, "timestamp.json"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}

	t.Run("serves static index", func(t *testing.T) {
		res, err := http.Get(baseURL + "/")
		if err != nil {
			t.Fatal(err)
		}
		defer res.Body.Close()

		if got, want := res.Header.Get("Content-Type"), "text/html; charset=utf-8"; got != want {
			t.Errorf("content-type: got %q, want %q", got, want)
			return
		}

		got, err := ioutil.ReadAll(res.Body)
		if err != nil {
			t.Error(err)
			return
		}
		if !bytes.Equal(got, []byte(pmhttp.HTML)) {
			t.Errorf("got %q, want %q", got, pmhttp.HTML)
		}
	})

	t.Run("serves js", func(t *testing.T) {
		res, err := http.Get(baseURL + "/js")
		if err != nil {
			t.Fatal(err)
		}
		defer res.Body.Close()

		if got, want := res.Header.Get("Content-Type"), "text/javascript; charset=utf-8"; got != want {
			t.Errorf("content-type: got %q, want %q", got, want)
			return
		}

		got, err := ioutil.ReadAll(res.Body)
		if err != nil {
			t.Error(err)
			return
		}
		if !bytes.Equal(got, []byte(pmhttp.JS)) {
			t.Errorf("got %q, want %q", got, pmhttp.JS)
		}
	})

	t.Run("serves config.json", func(t *testing.T) {
		res, err := http.Get(baseURL + "/config.json")
		if err != nil {
			t.Fatal(err)
		}
		defer res.Body.Close()

		if got, want := res.Header.Get("Content-Type"), "application/json"; got != want {
			t.Errorf("got %q, want %q", got, want)
		}

		var config pmhttp.Config
		if err := json.NewDecoder(res.Body).Decode(&config); err != nil {
			t.Fatalf("failed to decode config: %s", err)
		}

		if len(config.RootKeys) != 1 {
			t.Errorf("got %q, wanted 1", config.RootKeys)
		}

		// TODO: add some additional coverage for contents of the config file
	})

	t.Run("serves TUF jsons", func(t *testing.T) {
		for _, path := range []string{"/targets.json", "/timestamp.json", "/snapshot.json"} {
			res, err := http.Get(baseURL + path)
			if err != nil {
				t.Fatal(err)
			}
			defer res.Body.Close()

			if res.StatusCode != http.StatusOK {
				t.Errorf("GET %s: got %d, want %d", path, res.StatusCode, http.StatusOK)
				continue
			}

			if got, want := res.Header.Get("Content-Type"), "application/json"; got != want {
				t.Errorf("content-type: got %q, want %q", got, want)
				continue
			}

			want, err := ioutil.ReadFile(filepath.Join(repoDir, "repository", path))
			if err != nil {
				t.Error(err)
				continue
			}
			got, err := ioutil.ReadAll(res.Body)
			if err != nil {
				t.Error(err)
				continue
			}
			if !bytes.Equal(got, want) {
				t.Errorf("got %X, want %X", got, want)
			}
		}
	})

	t.Run("serves package blobs", func(t *testing.T) {
		m, err := cfg.OutputManifest()
		if err != nil {
			t.Fatal(err)
		}

		for _, blob := range m.Blobs {
			res, err := http.Get(baseURL + "/blobs/" + blob.Merkle.String())
			if err != nil {
				t.Fatal(err)
			}
			res.Body.Close()
			if res.ContentLength != int64(blob.Size) {
				t.Errorf("blob length: got %d, want %d", res.ContentLength, int64(blob.Size))
			}
		}
	})

	t.Run("auto-publishes new package version", func(t *testing.T) {
		if hasTarget(t, baseURL, "testpackage/1") {
			t.Fatalf("prematurely found target package")
		}

		cfg.PkgVersion = "1"
		build.BuildTestPackage(cfg)

		// expect an event when the package is updated
		event = cli.readEvent()
		if got, want := event.Event, "timestamp.json"; got != want {
			t.Errorf("got %q, want %q", got, want)
		}

		if !hasTarget(t, baseURL, "testpackage/1") {
			t.Fatal("missing target package")
		}
	})

	t.Run("writes port file", func(t *testing.T) {
		_, want, err := net.SplitHostPort(addr)
		if err != nil {
			t.Fatal(err)
		}
		got, err := ioutil.ReadFile(portFile)
		if err != nil {
			t.Fatal(err)
		}
		if want != string(got) {
			t.Errorf("got %s, want %s", got, want)
		}
	})
}

func TestServeAuto(t *testing.T) {
	defer resetFlags()
	defer resetServer()
	defer pushPopMonitorPollInterval(20 * time.Millisecond)()
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.BuildTestPackage(cfg)

	portFileDir := t.TempDir()
	portFile := filepath.Join(portFileDir, "port-file")
	repoDir := t.TempDir()
	repo, err := repo.New(repoDir)
	if err != nil {
		t.Fatal(err)
	}

	if err := repo.Init(); err != nil {
		t.Fatal(err)
	}

	if err := repo.AddTargets([]string{}, json.RawMessage{}); err != nil {
		t.Fatal(err)
	}

	if err := repo.CommitUpdates(false); err != nil {
		t.Fatal(err)
	}

	addrChan := make(chan string)
	var w sync.WaitGroup
	w.Add(1)
	go func() {
		defer w.Done()
		err := Run(cfg, []string{"-l", "127.0.0.1:0", "-repo", repoDir, "-f", portFile}, addrChan)
		if err != nil && err != http.ErrServerClosed {
			t.Fatal(err)
		}
	}()
	defer func() {
		server.Close()
		w.Wait()
	}()
	addr := <-addrChan
	baseURL := fmt.Sprintf("http://%s", addr)

	t.Run("sends events on timestamp version updates", func(t *testing.T) {
		cli := newTestAutoClient(t, baseURL)
		defer cli.close()

		// No initial events expected.
		cli.verifyNoPendingEvents()

		// Modifications to the file that don't change the version field are ignored
		if err := os.Chtimes(filepath.Join(repoDir, "repository", "timestamp.json"), time.Now(), time.Now()); err != nil {
			t.Fatal(err)
		}
		cli.verifyNoPendingEvents()

		// But actually changing the timestamp file does result in an event.
		if err := repo.SetTimestampVersion(42); err != nil {
			t.Fatal(err)
		}
		if err := repo.Commit(); err != nil {
			t.Fatal(err)
		}
		event := cli.readEvent()
		if got, want := event.Event, "timestamp.json"; got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	})

	t.Run("recovers from deleted timestamp", func(t *testing.T) {
		cli := newTestAutoClient(t, baseURL)
		defer cli.close()

		// No initial events expected.
		cli.verifyNoPendingEvents()

		// There should be a single event after committing to the repo.
		if err := repo.CommitUpdates(false); err != nil {
			t.Fatal(err)
		}
		event := cli.readEvent()
		if event.Event != "timestamp.json" {
			t.Fatalf("got %s, want timestamp.json", event.Event)
		}
		cli.verifyNoPendingEvents()

		// Even if timestamp.json is somehow deleted before/during a commit.
		if err := os.Remove(filepath.Join(repoDir, "repository", "timestamp.json")); err != nil {
			t.Fatal(err)
		}
		cli.verifyNoPendingEvents()

		// But re-creating it does.
		if err := repo.CommitUpdates(false); err != nil {
			t.Fatal(err)
		}
		event = cli.readEvent()
		if event.Event != "timestamp.json" {
			t.Fatalf("got %s, want timestamp.json", event.Event)
		}

		cli.verifyNoPendingEvents()
	})

	t.Run("recovers from moved timestamp", func(t *testing.T) {
		cli := newTestAutoClient(t, baseURL)
		defer cli.close()

		// No initial events expected.
		cli.verifyNoPendingEvents()

		// Renaming the file does not trigger an event.
		if err := os.Rename(filepath.Join(repoDir, "repository", "timestamp.json"), filepath.Join(repoDir, "repository", "timestamp2.json")); err != nil {
			t.Fatal(err)
		}
		cli.verifyNoPendingEvents()

		// But re-creating it does.
		if err := repo.CommitUpdates(false); err != nil {
			t.Fatal(err)
		}
		event := cli.readEvent()
		if event.Event != "timestamp.json" {
			t.Fatalf("got %s, want timestamp.json", event.Event)
		}
		cli.verifyNoPendingEvents()
	})

}

func TestServeAutoIncremental(t *testing.T) {
	defer resetFlags()
	defer resetServer()
	defer pushPopMonitorPollInterval(20 * time.Millisecond)()
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	portFileDir, err := ioutil.TempDir("", "pm-serve-test-port-file-dir")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(portFileDir)
	portFile := fmt.Sprintf("%s/%s", portFileDir, "port-file")

	repoDir, err := ioutil.TempDir("", "pm-serve-test-repo")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(repoDir)

	manifestListPath := filepath.Join(cfg.OutputDir, "pkg-manifests.list")
	pkgManifestPath := filepath.Join(cfg.OutputDir, "package_manifest.json")
	if err := ioutil.WriteFile(manifestListPath, []byte(pkgManifestPath+"\n"), 0644); err != nil {
		t.Fatal(err)
	}

	repo, err := repo.New(repoDir)
	if err != nil {
		t.Fatal(err)
	}

	if err := repo.Init(); err != nil {
		t.Fatal(err)
	}

	if err := repo.AddTargets([]string{}, json.RawMessage{}); err != nil {
		t.Fatal(err)
	}

	if err := repo.CommitUpdates(false); err != nil {
		t.Fatal(err)
	}

	addrChan := make(chan string)
	var w sync.WaitGroup
	w.Add(1)
	go func() {
		defer w.Done()
		err := Run(cfg, []string{"-l", "127.0.0.1:0", "-repo", repoDir, "-p", manifestListPath, "-f", portFile}, addrChan)
		if err != nil && err != http.ErrServerClosed {
			t.Fatal(err)
		}
	}()
	defer func() {
		server.Close()
		w.Wait()
	}()
	addr := <-addrChan
	baseURL := fmt.Sprintf("http://%s", addr)

	t.Run("auto-publishes packages incrementally built", func(t *testing.T) {
		if hasTarget(t, baseURL, "testpackage/0") {
			t.Fatalf("prematurely found target package")
		}

		if _, err := os.Stat(pkgManifestPath); err == nil {
			t.Fatalf("prematurely found target package manifest")
		}

		cli := newTestAutoClient(t, baseURL)
		defer cli.close()

		cli.verifyNoPendingEvents()

		build.BuildTestPackage(cfg)

		event := cli.readEvent()
		if got, want := event.Event, "timestamp.json"; got != want {
			t.Errorf("got %q, want %q", got, want)
		}

		if !hasTarget(t, baseURL, "testpackage/0") {
			t.Fatal("missing target package")
		}

		// Now delete the package manifest
		if err := os.Remove(pkgManifestPath); err != nil {
			t.Fatal(err)
		}

		cli.verifyNoPendingEvents()

		// Build it again
		build.BuildTestPackage(cfg)

		event = cli.readEvent()
		if got, want := event.Event, "timestamp.json"; got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	})
}

func hasTarget(t *testing.T, baseURL, target string) bool {
	res, err := http.Get(baseURL + "/targets.json")
	if err != nil {
		t.Fatal(err)
	}
	defer res.Body.Close()
	m := struct {
		Signed struct {
			Targets map[string]json.RawMessage
		}
	}{}
	if err := json.NewDecoder(res.Body).Decode(&m); err != nil {
		t.Fatal(err)
	}
	_, found := m.Signed.Targets[target]
	return found
}

type eventResult struct {
	event *sse.Event
	err   error
}

type testAutoClient struct {
	events chan eventResult
	done   chan struct{}
	resp   *http.Response
	t      *testing.T
}

func newTestAutoClient(t *testing.T, baseURL string) testAutoClient {
	req, err := http.NewRequest("GET", baseURL+"/auto", nil)
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Accept", "text/event-stream")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	cli, err := sse.New(resp)
	if err != nil {
		resp.Body.Close()
		t.Fatal(err)
	}

	events := make(chan eventResult)
	done := make(chan struct{})
	go func() {
		for {
			event, err := cli.ReadEvent()
			select {
			case events <- eventResult{event: event, err: err}:
				if err != nil {
					close(events)
					return
				}
			case <-done:
				close(events)
				return
			}
		}
	}()
	return testAutoClient{
		events: events,
		done:   done,
		resp:   resp,
		t:      t,
	}
}

func (c *testAutoClient) close() {
	c.resp.Body.Close()
	close(c.done)
}

func (c *testAutoClient) verifyNoPendingEvents() {
	c.t.Helper()
	select {
	case res := <-c.events:
		if res.err != nil {
			c.t.Fatalf("Unexpected error: %v", res.err)
		} else {
			c.t.Fatalf("Unexpected event: %#v", *res.event)
		}
	case <-time.After(time.Millisecond):
		// Give the server a bit of time to maybe send an event before deciding there isn't one
		// to be read.
	}
}

func (c *testAutoClient) readEvent() sse.Event {
	c.t.Helper()
	res := <-c.events
	if res.err != nil {
		c.t.Fatalf("Unexpected error: %v", res.err)
	}
	return *res.event
}
