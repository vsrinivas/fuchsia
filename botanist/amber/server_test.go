// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package amber_test

import (
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"strings"
	"testing"

	"fuchsia.googlesource.com/tools/botanist/amber"
	"fuchsia.googlesource.com/tools/color"
	"fuchsia.googlesource.com/tools/logger"
)

func TestServePackages(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Create a fake build output directory.
	root, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatalf("failed to create temp directory: %v", err)
	}
	defer os.RemoveAll(root)

	// Create a fake pakage repository with a single file to serve in that directory.
	packageRoot := path.Join(root, "amber-files", "repository")
	if err := os.MkdirAll(packageRoot, os.FileMode(0755)); err != nil {
		t.Fatalf("failed to create directory %s: %v", packageRoot, err)
	}

	file, err := ioutil.TempFile(packageRoot, "")
	if err != nil {
		t.Fatalf("failed to create tempfile in %s: %v", packageRoot, err)
	}

	const message = "test file"
	if _, err := file.Write([]byte(message)); err != nil {
		t.Fatalf("failed to write dummy data to %s: %v", file.Name(), err)
	}

	// Serve the repository.
	serverURL := amber.ServePackages(ctx, amber.ServerOptions{
		Address:      amber.DefaultServerAddress,
		PackagesPath: amber.DefaultPackagesPath(root),
	})

	// Ensure we can fetch the file.
	uri := fmt.Sprintf("%s/%s", serverURL, filepath.Base(file.Name()))
	res, err := http.Get(uri)
	if err != nil {
		t.Fatalf("failed to GET %q: %v", uri, err)
	}
	defer res.Body.Close()

	// Ensure the contents are as expected.
	data, err := ioutil.ReadAll(res.Body)
	if err != nil {
		t.Fatalf("failed to read response body: %v", err)
	}
	if string(data) != message {
		t.Fatalf("got %q but wanted %q", string(data), message)
	}
}

func TestServerWithMiddleware(t *testing.T) {
	out := new(bytes.Buffer)

	loggingMiddleware := func(next http.Handler) http.Handler {
		level := logger.InfoLevel
		l := logger.NewLogger(level, color.NewColor(color.ColorAuto), out, out)
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			l.Logf(level, "[%s] %q", r.Method, r.URL.Path)
			next.ServeHTTP(w, r)
		})
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	serverURL := amber.ServePackages(ctx, amber.ServerOptions{
		Address:      amber.DefaultServerAddress,
		PackagesPath: amber.DefaultPackagesPath("."),
		Middleware:   loggingMiddleware,
	})

	uri := fmt.Sprintf("%s/%s", serverURL, filepath.Base("unimportant"))
	res, err := http.Get(uri)
	if err != nil {
		t.Fatalf("failed to GET %q: %v", uri, err)
	}
	defer res.Body.Close()

	if out.String() == "" {
		t.Fatalf("middleware was not called")
	}

	expected := `[GET] "/unimportant"`
	actual := strings.TrimSpace(out.String())
	if actual != expected {
		t.Fatalf("got %q but wanted %q", actual, expected)
	}
}
