// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serve

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"time"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/sse"
)

type loggingWriter struct {
	http.ResponseWriter
	status int
}

func (lw *loggingWriter) WriteHeader(status int) {
	lw.status = status
	lw.ResponseWriter.WriteHeader(status)
}

func (lw *loggingWriter) Flush() {
	lw.ResponseWriter.(http.Flusher).Flush()
}

var (
	mu          sync.Mutex
	autoClients = map[http.ResponseWriter]struct{}{}
)

func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	repoDir := fs.String("d", "", "The path to the file repository to serve.")
	listen := fs.String("l", ":8083", "HTTP listen address")
	auto := fs.Bool("a", true, "Host auto endpoint for realtime client updates")
	autoRate := fs.Duration("auto-rate", time.Second, "rate at which to poll filesystem if realtime watch is not available")
	quiet := fs.Bool("q", false, "Don't print out information about requests")

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "usage: %s serve", filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	if *repoDir == "" {
		if buildDir, ok := os.LookupEnv("FUCHSIA_BUILD_DIR"); ok {
			*repoDir = filepath.Join(buildDir, "amber-files", "repository")
		} else {
			return fmt.Errorf("the FUCHSIA_BUILD_DIR environment variable should be set or supply a path with -d")
		}
	}

	fi, err := os.Stat(*repoDir)
	if err != nil {
		return fmt.Errorf("repository path %q is not valid: %s", *repoDir, err)
	}

	if !fi.IsDir() {
		return fmt.Errorf("repository path %q is not a directory", *repoDir)
	}

	if *auto {
		// TODO(raggi): move to fsnotify
		go func() {
			timestampPath := filepath.Join(*repoDir, "timestamp.json")
			lastUpdateTime := time.Now()
			t := time.NewTicker(*autoRate)
			for range t.C {
				fi, err := os.Stat(timestampPath)
				if err != nil {
					continue
				}

				if fi.ModTime().After(lastUpdateTime) {
					lastUpdateTime = fi.ModTime()
					mu.Lock()
					for w := range autoClients {
						// errors are ignored, as close notifier in the handler
						// ultimately handles cleanup
						sse.Write(w, &sse.Event{
							Event: "timestamp.json",
							Data:  []byte(lastUpdateTime.Format(http.TimeFormat)),
						})
					}
					mu.Unlock()
				}
			}
		}()

		http.HandleFunc("/auto", func(w http.ResponseWriter, r *http.Request) {
			err := sse.Start(w, r)
			if err != nil {
				log.Printf("SSE request failure: %s", err)
				w.WriteHeader(http.StatusInternalServerError)
				return
			}
			mu.Lock()
			autoClients[w] = struct{}{}
			defer func() {
				mu.Lock()
				delete(autoClients, w)
				mu.Unlock()
			}()
			mu.Unlock()
			<-r.Context().Done()
		})
	}

	http.Handle("/", http.FileServer(http.Dir(*repoDir)))

	if !*quiet {
		fmt.Printf("%s [pm serve] serving %s at http://%s\n",
			time.Now().Format("2006-01-02 15:04:05"), *repoDir, *listen)
	}

	return http.ListenAndServe(*listen, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		lw := &loggingWriter{w, 0}
		http.DefaultServeMux.ServeHTTP(lw, r)
		if !*quiet {
			fmt.Printf("%s [pm serve] %d %s\n",
				time.Now().Format("2006-01-02 15:04:05"), lw.status, r.RequestURI)
		}
	}))
}
