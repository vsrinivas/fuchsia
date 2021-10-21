// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pmhttp

import (
	"fmt"
	"io"
	"net"
	"net/http"
	"testing"
)

func TestLoggingWriter(t *testing.T) {
	msg := "hello world"

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(500)
		fmt.Fprintf(w, "%s", msg)
	})

	var lwStatus int
	var lwResponseSize int64

	server := &http.Server{
		Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			lw := &LoggingWriter{w, 0, 0}
			mux.ServeHTTP(lw, r)

			lwStatus = lw.Status
			lwResponseSize = lw.ResponseSize
		}),
	}
	defer server.Close()

	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		t.Errorf("error creating listener: %s", err)
		return
	}

	go func() {
		server.Serve(listener)
	}()

	resp, err := http.Get(fmt.Sprintf("http://%s/", listener.Addr().String()))
	if err != nil {
		t.Errorf("error making request: %s", err)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode != 500 {
		t.Errorf("wrong status, expected 500, got %d", resp.StatusCode)
		return
	}

	body, err := io.ReadAll(resp.Body)
	bodyString := string(body)
	if bodyString != msg {
		t.Errorf("got wrong body: expected %q, got %q", msg, bodyString)
		return
	}

	if lwStatus != 500 {
		t.Errorf("wrong status, expected 500, got %d", lwStatus)
	}

	if int(lwResponseSize) != len(msg) {
		t.Errorf("wrong response length, expected %d, got %d", len(msg), lwResponseSize)
	}
}
