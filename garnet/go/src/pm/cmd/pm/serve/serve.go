// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serve

import (
	"bufio"
	"compress/gzip"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
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

// server is a default http server only parameterized for tests.
var server http.Server

func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	repoDir := fs.String("d", "", "(deprecated, use -repo) path to the repository")

	config := &repo.Config{}
	config.Vars(fs)

	listen := fs.String("l", ":8083", "HTTP listen address")
	auto := fs.Bool("a", true, "Host auto endpoint for realtime client updates")
	quiet := fs.Bool("q", false, "Don't print out information about requests")
	encryptionKey := fs.String("e", "", "Path to a symmetric blob encryption key *UNSAFE*")
	publishList := fs.String("p", "", "path to a package list file to be auto-published")

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
		*repoDir = config.RepoDir
	}
	repoPubDir := filepath.Join(*repoDir, "repository")

	repo, err := repo.New(*repoDir)
	if err != nil {
		return err
	}
	if *encryptionKey != "" {
		repo.EncryptWith(*encryptionKey)
	}

	if err := repo.Init(); err != nil && err != os.ErrExist {
		return fmt.Errorf("repository at %q is not valid or could not be initialized: %s", *repoDir, err)
	}

	if *auto {
		as := pmhttp.NewAutoServer()

		w, err := fswatch.NewWatcher()
		if err != nil {
			return fmt.Errorf("failed to initialize fsnotify: %s", err)
		}

		publishAll := func() {
			if *publishList != "" {
				f, err := os.Open(*publishList)
				if err != nil {
					log.Printf("reading package list %q: %s", *publishList, err)
					return
				}
				defer f.Close()

				s := bufio.NewScanner(f)
				for s.Scan() {
					m := s.Text()
					if _, err := os.Stat(m); err == nil {
						repo.PublishManifest(m)
						if err := w.Add(m); err != nil {
							log.Printf("unable to watch %q", m)
						}
					} else {
						log.Printf("unable to publish %q", m)
					}
				}
				if err := repo.CommitUpdates(config.TimeVersioned); err != nil {
					log.Printf("committing repo: %s", err)
				}
			}
		}

		timestampPath := filepath.Join(repoPubDir, "timestamp.json")
		if err = w.Add(timestampPath); err != nil {
			return fmt.Errorf("failed to watch %s: %s", timestampPath, err)
		}
		if *publishList != "" {
			if err := w.Add(*publishList); err != nil {
				return fmt.Errorf("failed to watch %s: %s", *publishList, err)
			}
		}
		go func() {
			for event := range w.Events {
				switch event.Name {
				case timestampPath:
					fi, err := os.Stat(timestampPath)
					if err != nil {
						continue
					}
					as.Broadcast("timestamp.json", fi.ModTime().Format(http.TimeFormat))
				case *publishList:
					publishAll()
				default:
					if err := repo.PublishManifest(event.Name); err != nil {
						log.Printf("publishing %q: %s", event.Name, err)
						continue
					}
					if err := repo.CommitUpdates(config.TimeVersioned); err != nil {
						log.Printf("committing repo update %q: %s", event.Name, err)
					}
				}
			}
		}()

		http.Handle("/auto", as)
		publishAll()
	}

	dirServer := http.FileServer(http.Dir(repoPubDir))
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

	cs := pmhttp.NewConfigServer(func() []byte {
		b, err := ioutil.ReadFile(filepath.Join(repoPubDir, "root.json"))
		if err != nil {
			log.Printf("%s", err)
		}
		return b
	}, *encryptionKey)
	http.Handle("/config.json", cs)

	if !*quiet {
		fmt.Printf("%s [pm serve] serving %s at http://%s\n",
			time.Now().Format("2006-01-02 15:04:05"), *repoDir, *listen)
	}

	server.Addr = *listen
	server.Handler = http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
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
	})

	return server.ListenAndServe()
}
