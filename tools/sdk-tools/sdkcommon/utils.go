// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sdkcommon

import (
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"strings"
)

func runGSUtil(args []string) (string, error) {
	path, err := ExecLookPath("gsutil")
	if err != nil {
		return "", fmt.Errorf("could not find gsutil on path: %v", err)
	}
	cmd := ExecCommand(path, args...)
	out, err := cmd.Output()
	if err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			return "", fmt.Errorf("%v: %v", string(exitError.Stderr), exitError)
		}
		return "", err
	}
	return string(out), err
}

func runSSH(args []string, interactive bool) (string, error) {
	path, err := ExecLookPath("ssh")
	if err != nil {
		return "", fmt.Errorf("could not find ssh on path: %v", err)
	}
	cmd := ExecCommand(path, args...)
	if interactive {
		cmd.Stderr = os.Stderr
		cmd.Stdout = os.Stdout
		cmd.Stdin = os.Stdin
		return "", cmd.Run()
	}

	out, err := cmd.Output()
	if err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			return "", fmt.Errorf("%v: %v", string(exitError.Stderr), exitError)
		}
		return "", err
	}
	return string(out), err
}

func runSFTP(args []string, stdin string) error {
	path, err := ExecLookPath("sftp")
	if err != nil {
		return fmt.Errorf("could not find sftp on path: %v", err)
	}
	cmd := ExecCommand(path, args...)
	cmd.Stderr = os.Stderr
	cmd.Stdout = os.Stdout
	cmd.Stdin = strings.NewReader(stdin)
	return cmd.Run()
}

func GCSFileExists(gcsPath string) (string, error) {
	args := []string{"ls", gcsPath}
	return runGSUtil(args)
}

func GCSCopy(gcsSource string, localDest string) (string, error) {
	args := []string{"cp", gcsSource, localDest}
	return runGSUtil(args)
}

// FileExists returns true if filename exists.
func FileExists(filename string) bool {
	info, err := os.Stat(filename)
	if os.IsNotExist(err) {
		return false
	}
	return !info.IsDir()
}

// DirectoryExists returns true if dirname exists.
func DirectoryExists(dirname string) bool {
	info, err := os.Stat(dirname)
	if os.IsNotExist(err) {
		return false
	}
	return info.IsDir()
}

// WriteTempFile writes a file with content `contents` and returns the path
// to the file. The caller is responsible for cleaning up this file.
func WriteTempFile(contents []byte) (string, error) {
	f, err := ioutil.TempFile(os.TempDir(), "sdkcommon-")
	if err != nil {
		return "", err
	}
	defer f.Close()
	path := f.Name()
	if _, err = f.Write(contents); err != nil {
		return "", err
	}
	return path, nil
}
