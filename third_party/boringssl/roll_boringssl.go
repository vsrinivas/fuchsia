// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This script updates //third_party/boringssl/src to point to the current revision at:
//   https://boringssl.googlesource.com/boringssl/+/master
//
// It also updates the generated build files, Rust bindings, and subset of code used in Zircon.

package main

import (
	"bufio"
	"bytes"
	"flag"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

// Returns the path to the boringssl directory.
func configure() string {
	log.Println("Configuring...")
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		log.Fatal("failed to find current executable")
	}
	return filepath.Dir(file)
}

// Updates BoringSSL sources and returns the revision.
func updateSources(dir, commit string) []byte {
	log.Println("Updating BoringSSL sources...")
	dir = filepath.Join(dir, "src")
	{
		cmd := exec.Command("git", "-C", dir, "fetch", "--all", "--prune")
		if err := cmd.Run(); err != nil {
			log.Fatalf("%s failed: %s", cmd.Args, err)
		}
	}
	{
		cmd := exec.Command("git", "-C", dir, "checkout", commit)
		if err := cmd.Run(); err != nil {
			log.Fatalf("%s failed: %s", cmd.Args, err)
		}
	}
	{
		cmd := exec.Command("git", "-C", dir, "rev-parse", commit)
		out, err := cmd.Output()
		if err != nil {
			log.Fatalf("%s failed: %s", cmd.Args, err)
		}
		return bytes.TrimSpace(out)
	}
}

// Create the GN build files for the current sources.
func generateGN(dir string) {
	log.Printf("Generating build files...")
	cmd := exec.Command("python3", filepath.Join("src", "util", "generate_build_files.py"), "gn")
	cmd.Dir = dir
	if err := cmd.Run(); err != nil {
		log.Fatalf("%s failed: %s", cmd.Args, err)
	}
	exportTestData(dir)
}

func exportTestData(dir string) {
	// To control aggregate test binary size, we compile the test data
	// fixture as a shared library.  For linking to work when we do that,
	// we need to set the visibility attribute on the symbol we intend to
	// export, which is not done by the code from the default build file
	// generator.  So we post-process it here, adding default visibility to
	// the single function we intend to export.
	log.Printf("Marking GetTestData exported for shared library usage...")
	TEST_DATA_FILE := "crypto_test_data.cc"
	TEST_DATA_FILE_PATH := filepath.Join(dir, TEST_DATA_FILE)
	in, err := os.Open(TEST_DATA_FILE_PATH)
	if err != nil {
		log.Fatalf("Could not open %s: %s", TEST_DATA_FILE_PATH, err)
	}
	defer in.Close()
	out, err := os.CreateTemp(dir, TEST_DATA_FILE)
	if err != nil {
		log.Fatalf("Could not create output file for writing: %s", err)
	}
	defer out.Close()
	defer os.Remove(out.Name())

	seen := false
	scanner := bufio.NewScanner(in)
	writer := bufio.NewWriter(out)
	MATCH_PATTERN := "std::string GetTestData(const char *path)"
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, MATCH_PATTERN) {
			line = "__attribute__((__visibility__(\"default\"))) " + line
			seen = true
		}
		_, err := writer.WriteString(line + "\n")
		if err != nil {
			log.Fatalf("Could not write output to %s: %s", out.Name(), err)
		}
	}

	if !seen {
		log.Fatalf("Did not see any lines to modify in %s", TEST_DATA_FILE_PATH)
	}

	err = writer.Flush()
	if err != nil {
		log.Fatalf("Could not flush writes to %s: %s", out.Name(), err)
	}

	out.Close()
	err = os.Rename(out.Name(), TEST_DATA_FILE_PATH)
	if err != nil {
		log.Fatalf("Could not rename %s to %s: %s", out.Name(), TEST_DATA_FILE_PATH, err)
	}
}

// Regenerates the Rust bindings.
func generateRustBindings(dir string) {
	log.Printf("Generating Rust bindings...")
	cmd := exec.Command(filepath.Join("rust", "boringssl-sys", "bindgen.sh"))
	cmd.Dir = dir
	if err := cmd.Run(); err != nil {
		log.Fatalf("%s failed: %s", cmd.Args, err)
	}
}

// Updates the README file that ends with the current upstream git revision.
func updateReadMe(dir string, sha1 []byte) {
	const readmeName = "README.fuchsia"
	log.Printf("Updating %s...", readmeName)
	readme, err := os.OpenFile(filepath.Join(dir, readmeName), os.O_RDWR, 0644)
	if err != nil {
		log.Fatalf("failed to open %s: %s", readmeName, err)
	}
	defer func() {
		if err := readme.Close(); err != nil {
			log.Fatalf("failed to close %s: %s", readmeName, err)
		}
	}()

	// Assume that the file ends with a git URL.
	info, err := readme.Stat()
	if err != nil {
		log.Fatalf("failed to stat %s: %s", readmeName, err)
	}
	offset := info.Size() - int64(len(sha1)+2 /* trailing newlines */)
	if _, err = readme.WriteAt(sha1, offset); err != nil {
		log.Fatalf("failed to write to %s: %s", readmeName, err)
	}
}

func main() {
	commit := flag.String("commit", "origin/master", "Upstream commit-ish to check out")

	flag.Parse()

	log.SetFlags(log.Lmicroseconds)

	dir := configure()
	sha1 := updateSources(dir, *commit)

	log.Printf("Commit resolved to %s", sha1)

	generateGN(dir)
	generateRustBindings(dir)
	updateReadMe(dir, sha1)

	log.Println()
	log.Println("To test, run:")
	log.Println("  $ fx set ... --with //third_party/boringssl:tests")
	log.Println("  $ fx build")
	log.Println("  $ fx serve")
	log.Println("  $ fx test boringssl_tests")
	log.Println()
	log.Println("To verify Zircon linkage (see zircon-unused.c):")
	log.Println("  $ fx --dir out/bringup.x64.no_opt set bringup.x64 --args=optimize='\"none\"'")
	log.Println("  $ fx --dir out/bringup.x64.no_opt build")
	log.Println("  $ fx use default")

	log.Println("If tests pass; commit the changes in //third_party/boringssl (you may need to bypass the CQ)")
	log.Println("Then, update the BoringSSL revision in the internal integration repository")
}
