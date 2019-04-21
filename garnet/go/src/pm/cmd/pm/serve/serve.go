// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serve

import (
	"compress/gzip"
	"flag"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/fswatch"
	"fuchsia.googlesource.com/pm/pmhttp"
	"fuchsia.googlesource.com/pm/repo"
)

func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	repoDir := fs.String("d", "", "(deprecated, use -repo) path to the repository")

	config := &repo.Config{}
	config.Vars(fs)

	listen := fs.String("l", ":8083", "HTTP listen address")
	auto := fs.Bool("a", true, "Host auto endpoint for realtime client updates")
	quiet := fs.Bool("q", false, "Don't print out information about requests")
	encryptionKey := fs.String("e", "", "Path to a symmetric blob encryption key *UNSAFE*")

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "usage: %s serve", filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}
	config.ApplyDefaults()

	if *repoDir == "" {
		*repoDir = filepath.Join(config.RepoDir, "repository")
	}

	fi, err := os.Stat(*repoDir)
	if err != nil {
		return fmt.Errorf("repository path %q is not valid: %s", *repoDir, err)
	}

	if !fi.IsDir() {
		return fmt.Errorf("repository path %q is not a directory", *repoDir)
	}

	if *auto {
		as := pmhttp.NewAutoServer()

		w, err := fswatch.NewWatcher()
		if err != nil {
			return fmt.Errorf("failed to initialize fsnotify: %s", err)
		}
		timestampPath := filepath.Join(*repoDir, "timestamp.json")
		err = w.Add(timestampPath)
		if err != nil {
			return fmt.Errorf("failed to watch %s: %s", timestampPath, err)
		}
		go func() {
			for range w.Events {
				fi, err := os.Stat(timestampPath)
				if err != nil {
					continue
				}
				as.Broadcast("timestamp.json", fi.ModTime().Format(http.TimeFormat))
			}
		}()

		http.Handle("/auto", as)
	}

	dirServer := http.FileServer(http.Dir(*repoDir))
	http.Handle("/", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/":
			pmhttp.ServeIndex(w)
		case "/js":
			pmhttp.ServeJS(w)
		default:
			dirServer.ServeHTTP(w, r)
		}
	}))

	cs := pmhttp.NewConfigServer(*repoDir, *encryptionKey)
	http.Handle("/config.json", cs)

	if !*quiet {
		fmt.Printf("%s [pm serve] serving %s at http://%s\n",
			time.Now().Format("2006-01-02 15:04:05"), *repoDir, *listen)
	}

	return http.ListenAndServe(*listen, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.HasPrefix(r.RequestURI, "/blobs") && strings.Contains(r.Header.Get("Accept-Encoding"), "gzip") {
			gw := &pmhttp.GZIPWriter{
				w,
				gzip.NewWriter(w),
			}
			defer gw.Close()
			gw.Header().Set("Content-Encoding", "gzip")
			w = gw
		}
		lw := &pmhttp.LoggingWriter{w, 0}
		http.DefaultServeMux.ServeHTTP(lw, r)
		if !*quiet {
			fmt.Printf("%s [pm serve] %d %s\n",
				time.Now().Format("2006-01-02 15:04:05"), lw.Status, r.RequestURI)
		}
	}))
}
