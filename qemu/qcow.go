// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"io"
	"log"
	"os"
	"os/exec"
	"path"
	"path/filepath"
)

// CreateQCOWImage creates a qcow image (qcow2 format) with a given backing file.
//
// The backing file is copied the directory at the given output path.
// ------------------------------------------------------------------------------
//        \   ^__^
//         \  (oo)\_______
//            (__)\       )\/\
//                ||----w |
//                ||     ||
//
func CreateQCOWImage(qemuImgTool, backingFile, outputPath string) error {
	absToolPath, err := filepath.Abs(qemuImgTool)
	if err != nil {
		return err
	}

	// The tool expects the backing file to be in the same directory as the image.
	qcowDir := path.Dir(outputPath)
	backingFileName := path.Base(backingFile)
	in, err := os.Open(backingFile)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(filepath.Join(qcowDir, backingFileName))
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	if err != nil {
		return err
	}

	subcmd := []string{"create", "-f", "qcow2", "-b", backingFileName, outputPath}
	createQCOWCmd := exec.Command(absToolPath, subcmd...)
	createQCOWCmd.Stdout = os.Stdout
	createQCOWCmd.Stderr = os.Stderr
	log.Printf("running:\n\tArgs: %s\n\tEnv: %s", createQCOWCmd.Args, createQCOWCmd.Env)
	return createQCOWCmd.Run()
}
