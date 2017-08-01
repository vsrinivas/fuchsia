// Copyright 2017 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD - style license that can be
// found in the LICENSE file.

// This script is useful for checking whether the current source files under a
// boring-crypto directory are compatible with those in a boringssl repository.
// This can be used to determine whether any upstream changes need to be ported
// from boringssl to boring-crypto.

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
  DefaultMagenta = "//magenta/third_party/ulib/uboringssl"
)

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

// CheckReadMe returns whether the README.fuchsia.md in boring-crypto references
// the current boringssl revision or not.
func CheckReadMe(mxRoot, bsslRoot string) bool {
  cmd := exec.Command("git", "log", "-n1", "--pretty=%H")
  cmd.Dir = bsslRoot
  out, err := cmd.Output()
  if err != nil {
    log.Fatal(err)
  }
  file, err := os.Open(mxRoot + string(os.PathSeparator) + "README.fuchsia.md")
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

// IsValidAddedFile returns whether a file found only in boring-crypto and not
// in boringssl is a valid addition; that is, if it includes a Fuchsia copyright
// and ends in a -magenta.cpp suffix.
func IsValidAddedFile(path string) bool {
  if !strings.HasSuffix(path, "-magenta.cpp") {
    return false
  }
  file, err := os.Open(path)
  if err != nil {
    log.Fatal(err)
  }
  scanner := bufio.NewScanner(file)
  if !scanner.Scan() {
    if err := scanner.Err(); err != nil {
      log.Fatal(err)
    }
    return false
  }
  s := scanner.Text()
  return strings.Contains(s, "Copyright") && strings.Contains(s, "Fuchsia")
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

// IsSameFile returns whether a boring-crypto and boringssl file match, except
// for the allowable differences that are ignored by ReadNextLine.
func IsSameFile(mxPath string, bsslPath string) bool {
  same := true
  mxFile, err := os.Open(mxPath)
  if err != nil {
    log.Fatal(err)
  }
  bsslFile, err := os.Open(bsslPath)
  if err != nil {
    log.Fatal(err)
  }
  mxScan := bufio.NewScanner(mxFile)
  bsslScan := bufio.NewScanner(bsslFile)
  mxText, mxMore := ReadNextLine(mxScan)
  bsslText, bsslMore := ReadNextLine(bsslScan)
  for mxMore && bsslMore {
    if !mxMore {
      bsslText, bsslMore = ReadNextLine(bsslScan)
      same = false
      continue
    }
    if !bsslMore {
      mxText, mxMore = ReadNextLine(mxScan)
      same = false
      continue
    }
    if mxText != bsslText {
      same = false
    }
    mxText, mxMore = ReadNextLine(mxScan)
    bsslText, bsslMore = ReadNextLine(bsslScan)
  }
  return same
}

// CompareAll walks the files in boring-crypto and checks whether each is a
// valid addition or matches a corresponding file in boringssl.
func CompareAll(mxRoot, bsslRoot string) []string {
  files := []string{}
  walker := func(mxPath string, mxInfo os.FileInfo, err error) error {
    if err != nil {
      return err
    }
    if mxInfo.IsDir() {
      return nil
    }
    stem := mxPath[len(mxRoot):]
    bsslPath := bsslRoot + stem
    if _, err = os.Stat(bsslPath); err != nil {
      if !IsValidAddedFile(mxPath) {
        files = append(files, stem)
      }
  } else if !IsSameFile(mxPath, bsslPath) {
      files = append(files, stem)
    }
    return nil
  }
  stems := []string{"crypto", "include"}
  for _, stem := range stems {
    path := mxRoot + string(os.PathSeparator) + stem
    if err := filepath.Walk(path, walker); err != nil {
      log.Fatal(err)
    }
  }
  return files
}

// Main function
func main() {
  bsslFlag := flag.String("boringssl", DefaultBoringSSL, "BoringSSL repository")
  mxFlag := flag.String("boring-crypto", DefaultMagenta, "boring-crypto path")
  flag.Parse()
  mxRoot :=  GetRealPath(*mxFlag)
  bsslRoot :=  GetRealPath(*bsslFlag)
  ready := CheckReadMe(mxRoot, bsslRoot)
  if !ready {
    fmt.Println("[-] The README file needs to be updated.")
  }
  files := CompareAll(mxRoot, bsslRoot)
  if len(files) != 0 {
    fmt.Println("[-] The following files do not match:")
    for _, file := range files {
      fmt.Println("[-]   " + file)
    }
    ready = false
  }
  if ready {
    fmt.Printf("[+] %s and %s are in sync.\n", *mxFlag, *bsslFlag)
  } else {
    fmt.Printf("\n[-] %s and %s are NOT in sync.\n", *mxFlag, *bsslFlag)
  }
}
