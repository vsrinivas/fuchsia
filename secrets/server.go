// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package secrets

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"log"
	"net/http"
	"strconv"
	"strings"

	"go.chromium.org/luci/lucictx"
)

// A mapping of test name to associated secret.
type testSecrets map[string]string

// ServeSecret serves the secret associated to a test, where the request's URL is of the
// form "/<test name>".
func (secrets testSecrets) serveSecret(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	testName := strings.TrimPrefix(r.URL.Path, "/")
	secret, ok := secrets[testName]
	if !ok {
		log.Printf("There is no secret to serve for \"%s\"\n", testName)
		w.WriteHeader(http.StatusNotFound)
		return
	}
	log.Printf("Serving secret for \"%s\"\n", testName)
	log.Printf("SHA256 of secret: %x", sha256.Sum256([]byte(secret)))
	w.Header().Set("Content-Length", strconv.Itoa(len(secret)))
	w.WriteHeader(http.StatusOK)
	w.Write([]byte(secret))
}

// Parses out tests secrets serialized in the LUCI_CONTEXT JSON under the
// "secret_bytes" key.
func getSecrets(ctx context.Context) *testSecrets {
	swarming := lucictx.GetSwarming(ctx)
	if swarming == nil {
		return nil
	}
	secrets := new(testSecrets)
	if err := json.Unmarshal(swarming.SecretBytes, secrets); err != nil {
		log.Fatalf("secret_bytes provided, but unreadable: %v", err)
	}
	return secrets
}

// StartSecretsServer starts a server to serve test secrets at localhost:<|port|>.
func StartSecretsServer(ctx context.Context, port int) {
	secrets := getSecrets(ctx)
	if secrets == nil {
		return
	}

	log.Printf("Setting up secrets server at localhost:%d\n", port)
	s := &http.Server{
		Addr:    ":" + strconv.Itoa(port),
		Handler: http.HandlerFunc(secrets.serveSecret),
	}

	go func() {
		if err := s.ListenAndServe(); err != nil {
			log.Print(err)
		}
	}()

	go func() {
		select {
		case <-ctx.Done():
			log.Printf("Shutting down secrets server at localhost:%d\n", port)
			if err := s.Shutdown(context.Background()); err != nil {
				log.Print(err)
			}
		default:
		}
	}()
}
