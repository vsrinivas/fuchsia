// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package omaha

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"
	"regexp"
	"strings"

	//	"fuchsia.googlesource.com/host_target_testing/avb/avb"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

// TODO(vfcc): Waiting on fxr/382977
// avbtool      *avb.AVBTool
type OmahaServer struct {
	url          string
	updateHost   string
	updatePkg    string
	server       *http.Server
	mux          *http.ServeMux
	shuttingDown chan struct{}
}

type timestamp struct {
	ElapsedSeconds int `json:"elapsed_seconds"`
	ElapsedDays    int `json:"elapsed_days"`
}

type url struct {
	Codebase string `json:"codebase"`
}

type urls struct {
	Url []url `json:"url"`
}

type pkg struct {
	Name     string `json:"name"`
	Fp       string `json:"fp"`
	Required bool   `json:"required"`
}

type packages struct {
	Pkg []pkg `json:"package"`
}

type action struct {
	Run   string `json:"run,omitempty"`
	Event string `json:"event"`
}

type actions struct {
	Action []action `json:"package"`
}

type manifest struct {
	Version  string   `json:"version"`
	Actions  actions  `json:"actions"`
	Packages packages `json:"packages"`
}

type updateCheck struct {
	Status   string   `json:"status"`
	Urls     urls     `json:"urls"`
	Manifest manifest `json:"manifest"`
}

type app struct {
	CohortHint  string      `json:"cohorthint"`
	AppId       string      `json:"appid"`
	Cohort      string      `json:"cohort"`
	Status      string      `json:"status"`
	CohortName  string      `json:"cohortname"`
	UpdateCheck updateCheck `json:"updatecheck"`
}

type ResponseConfig struct {
	Server   string    `json:"server"`
	Protocol string    `json:"protocol"`
	DayStart timestamp `json:"timestamp"`
	App      []app     `json:"app"`
}

// NewOmahaServer starts an http server that serves the omaha response.
func NewOmahaServer(ctx context.Context, localHostname string) (*OmahaServer, error) {
	l := logger.NewLogger(
		logger.DebugLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		"omaha-server: ")
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		return nil, err
	}

	port := listener.Addr().(*net.TCPAddr).Port
	logger.Infof(ctx, "Serving Omaha from %d", port)

	hostname := strings.ReplaceAll(localHostname, "%", "%25")

	var serverURL string
	if strings.Contains(hostname, ":") {
		// This is an IPv6 address, use brackets for an IPv6 literal
		serverURL = fmt.Sprintf("http://[%s]:%d", hostname, port)
	} else {
		serverURL = fmt.Sprintf("http://%s:%d", hostname, port)
	}

	mux := http.NewServeMux()
	server := &http.Server{
		Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			logger.Infof(ctx, "Got request %s", r.RequestURI)
			mux.ServeHTTP(w, r)
		}),
	}

	go func() {
		if err := server.Serve(listener); err != http.ErrServerClosed {
			logger.Fatalf(ctx, "failed to shutdown server: %s", err)
		}
	}()

	shuttingDown := make(chan struct{})

	// Tear down the server if the context expires.
	go func() {
		select {
		case <-shuttingDown:
		case <-ctx.Done():
			server.Shutdown(ctx)
		}
	}()

	o := OmahaServer{url: serverURL, updateHost: "", updatePkg: "", server: server, mux: mux, shuttingDown: shuttingDown}
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		if len(o.updateHost) == 0 || len(o.updatePkg) == 0 {
			w.Write([]byte(`{"status": "noupdate"}`))
		} else {
			j, err := json.Marshal(&ResponseConfig{
				Server:   "prod",
				Protocol: "3.0",
				DayStart: timestamp{
					ElapsedSeconds: 400,
					ElapsedDays:    200,
				},
				App: []app{{
					CohortHint: "integration-test",
					AppId:      "some-id",
					Cohort:     "1:1:",
					Status:     "ok",
					CohortName: "integration-test-name",
					UpdateCheck: updateCheck{
						Status: "ok",
						Urls: urls{
							Url: []url{{Codebase: o.updateHost}},
						},
						Manifest: manifest{
							Version: "0.1.2.3",
							Actions: actions{
								Action: []action{{
									Run:   o.updatePkg,
									Event: "install",
								},
									{
										Event: "postinstall",
									}},
							},
							Packages: packages{
								Pkg: []pkg{{
									Name:     o.updatePkg,
									Fp:       "2.0.1.2.3",
									Required: true,
								}},
							},
						},
					}},
				},
			})
			if err != nil {
				logger.Infof(ctx, "Could not marshal JSON")
			}
			w.Write(j)
		}
	})
	return &o, nil
}

// Shutdown shuts down the Omaha Server.
func (o *OmahaServer) Shutdown(ctx context.Context) {
	o.server.Shutdown(ctx)
	close(o.shuttingDown)
}

// URL returns the URL of the Omaha Server that the target can use to talk to Omaha.
func (o *OmahaServer) URL() string {
	return o.url
}

// Service this update package URL as the current update package.
func (o *OmahaServer) SetUpdatePkgURL(updatePkgURL string) error {
	// Expected input format: fuchsia-pkg://fuchsia.com/update?hash=abcdef
	urlMatcher := regexp.MustCompile("(fuchsia-pkg://.+)/(.+)")
	matches := urlMatcher.FindStringSubmatch(updatePkgURL)
	if matches != nil && len(matches) == 3 {
		o.updateHost = matches[1]
		o.updatePkg = matches[2]
		return nil
	}
	return fmt.Errorf("Invalid updatePkgURL: %s", updatePkgURL)
}
