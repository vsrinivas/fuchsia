// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This script updates //src/security/bin/root_ssl_certificates/third_party/cert/cert.pem to point to
// the current revision at:
//   https://hg.mozilla.org/mozilla-central/raw-file/tip/security/nss/lib/ckfw/builtins/certdata.txt
// The resulting cert.pem file is licensed under the third_party/cert/LICENSE.MPLv2 file

package main

import (
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"strings"
	"time"
)

const bundleURL = "https://hg.mozilla.org/mozilla-central/raw-file/tip/security/nss/lib/ckfw/builtins/certdata.txt"
const bundleRetries = 3

var (
	bundle = flag.String("bundle", bundleURL, "URL to retrieve certificates from")
)

func updateBundle() {
	infof("  Fetching root certificates...")

	var response *http.Response
	var err error
	for i := 0; i < bundleRetries; i += 1 {
		if response, err = http.Get(*bundle); err == nil {
			break
		}
		log.Println(err)
	}
	if err != nil {
		log.Fatal(err)
	}
	defer response.Body.Close()
	certdata, err := io.ReadAll(response.Body)
	if err != nil {
		log.Fatal(err)
	}

	// we hash the contents of certdata.txt to see if the downloaded version
	// contains any updates, so touch it to ensure it exists first
	run("", "touch", "third_party/cert/certdata.txt")
	textfile := "third_party/cert/certdata.txt"
	olddigest := sha256sum(textfile)
	rawdigest := sha256.Sum256(certdata)
	hexdigest := hex.EncodeToString(rawdigest[:])
	if olddigest == hexdigest {
		infof("  Root certificates unchanged.")
		return
	}

	stamp := "URL:    " + *bundle + "\n"
	stamp += "SHA256: " + hexdigest + "\n"
	stamp += "Time:   " + time.Now().String() + "\n"

	stampfile, err := os.Create("third_party/cert/cert.stamp")
	if err != nil {
		log.Fatal(err)
	}
	defer stampfile.Close()

	if _, err = stampfile.WriteString(stamp); err != nil {
		log.Fatal(err)
	}

	if err := os.WriteFile("third_party/cert/certdata.txt", certdata, 0644); err != nil {
		log.Fatal(err)
	}

	infof("  Converting to PEM...")
	// cd into "third_party" and then run the "convert_mozilla_certdata.go" script.
	// We need to be in the same directory as the "certdata.txt" file because
	// "convert_mozilla_certdata.go" assumes it's in PWD.
	out := run("third_party/cert", "go", "run", "../convert_mozilla_certdata/convert_mozilla_certdata.go")
	if err := os.WriteFile("third_party/cert/cert.pem", out, 0644); err != nil {
		log.Fatal(err)
	}
}

func main() {
	flag.Parse()
	infof("Updating root certificates...")
	updateBundle()
	infof("Done!")
}

// Utility functions

func infof(msg string) {
	log.Printf("[+] %s\n", msg)
}

// sha256Sum returns the hex-encoded SHA256 digest of a file
func sha256sum(path string) string {
	file, err := os.Open(path)
	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()
	digest := sha256.New()
	if _, err := io.Copy(digest, file); err != nil {
		log.Fatal(err)
	}
	return hex.EncodeToString(digest.Sum(nil))
}

// Executes a command with the given |name| and |args| using |cwd| as the current working directory.
func run(cwd string, name string, args ...string) []byte {
	cmd := exec.Command(name, args...)
	if len(cwd) > 0 {
		cmd.Dir = cwd
	}
	out, err := cmd.CombinedOutput()
	if err != nil {
		cmdline := strings.Join(append([]string{name}, args...), " ")
		log.Printf("<!> Error returned for '" + cmdline + "'")
		log.Printf("<!> Output: " + string(out))
		log.Fatal(err)
	}
	return out
}
