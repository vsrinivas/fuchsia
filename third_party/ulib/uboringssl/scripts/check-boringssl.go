// Copyright 2017 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD - style license that can be
// found in the LICENSE file.

// This script is useful for checking whether the current source files under a
// uboringssl directory are compatible with those in a boringssl repository.
// This can be used to determine whether any upstream changes need to be ported
// from boringssl to uboringssl.

package main

import (
  "bufio"
  "flag"
  "fmt"
  "log"
  "os"
  "os/exec"
  "path/filepath"
  "strings"
)

const (
  DefaultBoringSSL = "//third_party/boringssl"
  DefaultZircon = "//zircon/third_party/ulib/uboringssl"
)

// These files are special; they are either auto-generated, manually modified, or explicitly added.
var skipped_files = map[string]bool {
  "/crypto/cpu-aarch64-zircon.cpp": true,
  "/crypto/err/err_data.c": true,
  "/include/openssl/base.h": true,
}

// GetRealPath returns a canonical path, with '//' replaced by $FUCHSIA_DIR
func GetRealPath(path string) string {
  if strings.HasPrefix(path, "//") {
    root, set := os.LookupEnv("FUCHSIA_DIR")
    if !set {
      log.Fatal("FUCHSIA_DIR not set")
    }
    path = root + string(os.PathSeparator) + path[2:]
  }
  var err error
  if path, err = filepath.Abs(path); err != nil {
    log.Fatal(err)
  }
  if path, err = filepath.EvalSymlinks(path); err !=nil {
    log.Fatal(err)
  }
  return path
}

// CheckReadMe returns whether the README.fuchsia.md in uboringssl references
// the current boringssl revision or not.
func CheckReadMe(zxRoot, bsslRoot string) bool {
  cmd := exec.Command("git", "log", "-n1", "--pretty=%H")
  cmd.Dir = bsslRoot
  out, err := cmd.Output()
  if err != nil {
    log.Fatal(err)
  }
  file, err := os.Open(zxRoot + string(os.PathSeparator) + "README.fuchsia.md")
  if err != nil {
    log.Fatal(err)
  }
  scanner := bufio.NewScanner(file)
  for scanner.Scan() {
    if strings.Contains(scanner.Text(), strings.TrimSpace(string(out))) {
      return true
    }
  }
  if err := scanner.Err(); err != nil {
    log.Fatal(err)
  }
  return false
}

// ReadNextLine reads the next newline-delimited line of text from a source
// file, skipping over _KERNEL guards and any code within __Fuchsia__ guards.
func ReadNextLine(scanner *bufio.Scanner) (string, bool) {
  ignoring := false
  for scanner.Scan() {
    text := scanner.Text()
    if ignoring || strings.Contains(text, "_KERNEL") {
      continue
    }
    if strings.Contains(text, "__Fuchsia__") {
      ignoring = !ignoring
      continue
    }
    return text, true
  }
  err := scanner.Err()
  if err != nil {
    log.Fatal(err)
  }
  return "", false
}

// IsSameFile returns whether a uboringssl and boringssl file match, except
// for the allowable differences that are ignored by ReadNextLine.
func IsSameFile(zxPath string, bsslPath string) bool {
  same := true
  zxFile, err := os.Open(zxPath)
  if err != nil {
    log.Fatal(err)
  }
  bsslFile, err := os.Open(bsslPath)
  if err != nil {
    log.Fatal(err)
  }
  zxScan := bufio.NewScanner(zxFile)
  bsslScan := bufio.NewScanner(bsslFile)
  zxText, zxMore := ReadNextLine(zxScan)
  bsslText, bsslMore := ReadNextLine(bsslScan)
  for zxMore && bsslMore {
    if !zxMore {
      bsslText, bsslMore = ReadNextLine(bsslScan)
      same = false
      continue
    }
    if !bsslMore {
      zxText, zxMore = ReadNextLine(zxScan)
      same = false
      continue
    }
    if zxText != bsslText {
      same = false
    }
    zxText, zxMore = ReadNextLine(zxScan)
    bsslText, bsslMore = ReadNextLine(bsslScan)
  }
  return same
}

// CompareAll walks the files in uboringssl and checks whether each is a
// valid addition or matches a corresponding file in boringssl.
func CompareAll(zxRoot, bsslRoot string) []string {
  files := []string{}
  walker := func(zxPath string, zxInfo os.FileInfo, err error) error {
    if err != nil {
      return err
    }
    if zxInfo.IsDir() {
      return nil
    }
    stem := zxPath[len(zxRoot):]
    if skipped_files[stem] {
      return nil
    }
    bsslPath := bsslRoot + stem
    if _, err = os.Stat(bsslPath); err != nil || !IsSameFile(zxPath, bsslPath) {
      files = append(files, stem)
    }
    return nil
  }
  stems := []string{"crypto", "decrepit", "include"}
  for _, stem := range stems {
    path := zxRoot + string(os.PathSeparator) + stem
    if err := filepath.Walk(path, walker); err != nil {
      log.Fatal(err)
    }
  }
  return files
}

// Main function
func main() {
  bsslFlag := flag.String("boringssl", DefaultBoringSSL, "BoringSSL repository")
  zxFlag := flag.String("uboringssl", DefaultZircon, "uboringssl path")
  flag.Parse()
  zxRoot :=  GetRealPath(*zxFlag)
  bsslRoot :=  GetRealPath(*bsslFlag)
  ready := CheckReadMe(zxRoot, bsslRoot)
  if !ready {
    fmt.Println("[-] The README file needs to be updated.")
  }
  files := CompareAll(zxRoot, bsslRoot)
  if len(files) != 0 {
    fmt.Println("[-] The following files do not match:")
    for _, file := range files {
      fmt.Println("[-]   " + file)
    }
    ready = false
  }
  if ready {
    fmt.Printf("[+] %s and %s are in sync.\n", *zxFlag, *bsslFlag)
  } else {
    fmt.Printf("\n[-] %s and %s are NOT in sync.\n", *zxFlag, *bsslFlag)
  }
}
