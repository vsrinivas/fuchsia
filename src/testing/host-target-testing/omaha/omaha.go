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
	"net/url"
	"os"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type OmahaServer struct {
	serverURL    string
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

type omahaURL struct {
	Codebase string `json:"codebase"`
}

type omahaURLs struct {
	Url []omahaURL `json:"url"`
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
	Action []action `json:"action"`
}

type manifest struct {
	Version  string   `json:"version"`
	Actions  actions  `json:"actions"`
	Packages packages `json:"packages"`
}

type updateCheck struct {
	Status   string    `json:"status"`
	Urls     omahaURLs `json:"urls"`
	Manifest manifest  `json:"manifest"`
}

type app struct {
	CohortHint  string      `json:"cohorthint"`
	AppId       string      `json:"appid"`
	Cohort      string      `json:"cohort"`
	Status      string      `json:"status"`
	CohortName  string      `json:"cohortname"`
	UpdateCheck updateCheck `json:"updatecheck"`
}

type responseConfig struct {
	Server   string    `json:"server"`
	Protocol string    `json:"protocol"`
	DayStart timestamp `json:"timestamp"`
	App      []app     `json:"app"`
}

type response struct {
	Response responseConfig `json:"response"`
}

type requestApp struct {
	AppId       string      `json:"appid"`
	Cohort      string      `json:"cohort,omitempty"`
	CohortHint  string      `json:"cohorthint,omitempty"`
	CohortName  string      `json:"cohortname,omitempty"`
	UpdateCheck interface{} `json:"updatecheck,omitempty"`
}

type requestConfig struct {
	Protocol string       `json:"protocol"`
	App      []requestApp `json:"app"`
}

type request struct {
	Request requestConfig `json:"request"`
}

// NewOmahaServer starts an http server that serves the omaha response. The
// `serverAddress` is the address the server will listen on. The
// `localHostname` is the hostname running the server from the perspective of
// the test device.
func NewOmahaServer(ctx context.Context, serverAddress string, localHostname string) (*OmahaServer, error) {
	l := logger.NewLogger(
		logger.DebugLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		"omaha-server: ")
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	listener, err := net.Listen("tcp", serverAddress)
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

	o := OmahaServer{
		serverURL:    serverURL,
		updateHost:   "",
		updatePkg:    "",
		server:       server,
		mux:          mux,
		shuttingDown: shuttingDown,
	}
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		var omahaRequest request
		decoder := json.NewDecoder(r.Body)
		if err := decoder.Decode(&omahaRequest); err != nil {
			logger.Infof(ctx, "Could not decode JSON: %v", err)
			w.WriteHeader(400)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		if len(o.updateHost) == 0 || len(o.updatePkg) == 0 {
			w.Write([]byte(`{"status": "noupdate"}`))
		} else {
			j, err := json.Marshal(&response{
				Response: responseConfig{
					Server:   "prod",
					Protocol: "3.0",
					DayStart: timestamp{
						ElapsedSeconds: 400,
						ElapsedDays:    200,
					},
					App: []app{{
						CohortHint: "a-cohort-hint",
						AppId:      omahaRequest.Request.App[0].AppId,
						Cohort:     "1:1:",
						Status:     "ok",
						CohortName: "a-cohort-name",
						UpdateCheck: updateCheck{
							Status: "ok",
							Urls: omahaURLs{
								Url: []omahaURL{{Codebase: o.updateHost}},
							},
							Manifest: manifest{
								Version: "0.1.2.3",
								Actions: actions{
									Action: []action{{
										Run:   o.updatePkg,
										Event: "update",
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
	return o.serverURL
}

// Service this update package URL as the current update package.
func (o *OmahaServer) SetUpdatePkgURL(ctx context.Context, updatePkgURL string) error {
	// Expected input format: fuchsia-pkg://fuchsia.com/update?hash=abcdef
	u, err := url.Parse(updatePkgURL)
	if err != nil {
		return fmt.Errorf("invalid update package URL %q: %w", updatePkgURL, err)
	}

	if u.Scheme != "fuchsia-pkg" {
		return fmt.Errorf("scheme must be fuchsia-pkg, not %q", u.Scheme)
	}

	if u.Host == "" {
		return fmt.Errorf("update package URL's host must not be empty")
	}

	logger.Infof(ctx, "Omaha Server update package set to %q %q", u.Host, u.String())

	o.updateHost = fmt.Sprintf("fuchsia-pkg://%s", u.Host)

	logger.Infof(ctx, "Omaha Server update package set to %q", u.String())

	// The updatePkg field is the URL with the scheme and host stripped off.
	u.Scheme = ""
	u.Host = ""
	o.updatePkg = u.String()

	return nil
}
