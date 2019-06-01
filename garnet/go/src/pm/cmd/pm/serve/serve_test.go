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

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/pmhttp"
	"fuchsia.googlesource.com/pm/repo"
	"fuchsia.googlesource.com/sse"
)

func TestParseFlags(t *testing.T) {
	// poor mans reset, for the cases covered below
	resetFlags := func() {
		config.RepoDir = ""
		*repoServeDir = ""
	}
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
	cfg := build.TestConfig()
	build.BuildTestPackage(cfg)

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

	if err := repo.GenKeys(); err != nil {
		t.Fatal(err)
	}

	if err := repo.AddTargets([]string{}, json.RawMessage{}); err != nil {
		t.Fatal(err)
	}

	if err := repo.CommitUpdates(false); err != nil {
		t.Fatal(err)
	}

	port := getPort()
	listen := fmt.Sprintf("127.0.0.1:%d", port)

	var w sync.WaitGroup
	w.Add(1)
	go func() {
		defer w.Done()
		err := Run(cfg, []string{"-l", listen, "-repo", repoDir, "-p", manifestListPath})
		if err != nil && err != http.ErrServerClosed {
			t.Fatal(err)
		}
	}()
	defer func() {
		server.Close()
		w.Wait()
	}()
	baseURL := fmt.Sprintf("http://%s", listen)

	// without restructuring the runtime significantly, there's not a good way to
	// detect the server is up, so we try a few times and fail if there are no
	// successes.
	for i := 0; i < 10; i++ {
		_, err := http.Get(baseURL + "/")
		if err != nil {
			time.Sleep(time.Second / 10)
			continue
		}
		break
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

	t.Run("serves /auto and events on timestamp updates", func(t *testing.T) {
		req, err := http.NewRequest("GET", baseURL+"/auto", nil)
		if err != nil {
			t.Fatal(err)
		}
		req.Header.Set("Accept", "text/event-stream")
		res, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		defer res.Body.Close()
		cli, err := sse.New(res)
		if err != nil {
			t.Fatal(err)
		}

		if err := os.Chtimes(filepath.Join(repoDir, "repository", "timestamp.json"), time.Now(), time.Now()); err != nil {
			t.Fatal(err)
		}

		event, err := cli.ReadEvent()
		if err != nil {
			t.Fatal(err)
		}
		if got, want := event.Event, "timestamp.json"; got != want {
			t.Errorf("got %q, want %q", got, want)
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
		if hasTarget(baseURL, "testpackage/1") {
			t.Fatalf("prematurely found target package")
		}

		// after building, we need to wait for publication to complete, so start a client for that
		req, err := http.NewRequest("GET", baseURL+"/auto", nil)
		if err != nil {
			t.Fatal(err)
		}
		req.Header.Set("Accept", "text/event-stream")
		res, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		defer res.Body.Close()
		cli, err := sse.New(res)
		if err != nil {
			t.Fatal(err)
		}

		cfg.PkgVersion = "1"
		build.BuildTestPackage(cfg)

		cli.ReadEvent()

		if !hasTarget(baseURL, "testpackage/1") {
			t.Fatal("missing target package")
		}
	})
}

func hasTarget(baseURL, target string) bool {
	res, err := http.Get(baseURL + "/targets.json")
	if err != nil {
		panic(err)
	}
	defer res.Body.Close()
	m := struct {
		Signed struct {
			Targets map[string]json.RawMessage
		}
	}{}
	if err := json.NewDecoder(res.Body).Decode(&m); err != nil {
		panic(err)
	}
	// FIXME(PKG-753) G1 -> G2 migration of TUF metadata.
	for _, target := range []string{"/" + target, target} {
		if _, found := m.Signed.Targets[target]; found {
			return true
		}
	}
	return false
}

// get a free port, with a very small chance of race
func getPort() int {
	s, err := net.ListenTCP("tcp", &net.TCPAddr{net.IPv4(127, 0, 0, 1), 0, ""})
	if err != nil {
		panic(err)
	}
	defer s.Close()
	return s.Addr().(*net.TCPAddr).Port
}
