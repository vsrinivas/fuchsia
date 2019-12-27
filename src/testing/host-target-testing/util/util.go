// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"archive/tar"
	"bufio"
	"bytes"
	"compress/gzip"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// RunCommand executes a command on the host and returns the stdout and stderr
// as byte strings.
func RunCommand(name string, arg ...string) ([]byte, []byte, error) {
	log.Printf("running: %s %q", name, arg)
	c := exec.Command(name, arg...)
	var o bytes.Buffer
	var e bytes.Buffer
	c.Stdout = &o
	c.Stderr = &e
	err := c.Run()
	stdout := o.Bytes()
	stderr := e.Bytes()
	return stdout, stderr, err
}

// Untar untars a tar.gz file into a directory.
func Untar(dst string, src string) error {
	log.Printf("untarring %s into %s", src, dst)

	f, err := os.Open(src)
	if err != nil {
		return err
	}
	defer f.Close()

	gz, err := gzip.NewReader(f)
	if err != nil {
		return err
	}
	defer gz.Close()

	tr := tar.NewReader(gz)

	for {
		header, err := tr.Next()
		if err == io.EOF {
			return nil
		} else if err != nil {
			return err
		}

		path := filepath.Join(dst, header.Name)
		info := header.FileInfo()
		if info.IsDir() {
			if err := os.MkdirAll(path, info.Mode()); err != nil {
				return err
			}
		} else {
			if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
				return err
			}

			if _, err = os.Stat(path); err == nil {
				continue
			}

			err := AtomicallyWriteFile(path, info.Mode(), func(f *os.File) error {
				_, err := io.Copy(f, tr)
				return err
			})
			if err != nil {
				return err
			}
		}
	}
}

func ParsePackageList(rd io.Reader) (map[string]string, error) {
	scanner := bufio.NewScanner(rd)
	packages := make(map[string]string)
	for scanner.Scan() {
		s := strings.TrimSpace(scanner.Text())
		entry := strings.Split(s, "=")
		if len(entry) != 2 {
			return nil, fmt.Errorf("parser: entry format: %q", s)
		}
		packages[entry[0]] = entry[1]
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return packages, nil
}

func AtomicallyWriteFile(path string, mode os.FileMode, writeFileFunc func(*os.File) error) error {
	dir := filepath.Dir(path)
	basename := filepath.Base(path)

	tmpfile, err := ioutil.TempFile(dir, basename)
	defer func() {
		if tmpfile != nil {
			os.Remove(tmpfile.Name())
		}
	}()

	if err = writeFileFunc(tmpfile); err != nil {
		return err
	}

	if err = os.Chmod(tmpfile.Name(), mode); err != nil {
		return err
	}

	// Now that we've written the file, do an atomic swap of the filename into place.
	if err := os.Rename(tmpfile.Name(), path); err != nil {
		return err
	}
	tmpfile = nil

	return nil
}
