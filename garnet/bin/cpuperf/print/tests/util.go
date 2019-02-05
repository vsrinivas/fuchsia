// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): This file exists to keep the build happy.
// It needs to see a .go file that isn't foo_test.go.
// Since we need to have this file we put some useful stuff here.

package tests

import (
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"os/exec"
)

func runCommandWithOutputToFile(command string, args []string,
	output io.Writer) error {
	// This doesn't use testing.Logf or some such because we always
	// want to see this, especially when run on bots.
	fmt.Printf("Running %s %v\n", command, args)
	cmd := exec.Command(command)
	cmd.Args = append(cmd.Args, args...)
	// There's no point to distinguishing stdout,stderr here.
	cmd.Stdout = output
	cmd.Stderr = output
	err := cmd.Run()
	if err != nil {
		fmt.Printf("Running %s failed: %s\n", command, err.Error())
	}
	return err
}

func compareFiles(file1, file2 string) error {
	contents1, err := ioutil.ReadFile(file1)
	if err != nil {
		return fmt.Errorf("Unable to read %s: %s", file1, err.Error())
	}
	contents2, err := ioutil.ReadFile(file2)
	if err != nil {
		return fmt.Errorf("Unable to read %s: %s", file2, err.Error())
	}

	if bytes.Compare(contents1, contents2) != 0 {
		return fmt.Errorf("Match failure")
	}
	return nil
}
