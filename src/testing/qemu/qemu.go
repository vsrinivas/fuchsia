// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"archive/tar"
	"bufio"
	"compress/gzip"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// Untar untars a tar.gz file into a directory.
func untar(dst string, src string) error {
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

			f, err := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, info.Mode())
			if err != nil {
				return err
			}

			if _, err := io.Copy(f, tr); err != nil {
				f.Close()
				return err
			}

			f.Close()
		}
	}
}

// Distribution is a collection of QEMU-related artifacts.
type Distribution struct {
	exPath       string
	unpackedPath string
}

type Arch int

const (
	X64 Arch = iota
	Arm64
)

// Params describes how to run a QEMU instance.
type Params struct {
	Arch Arch
	ZBI  string
}

type Instance struct {
	cmd    *exec.Cmd
	stdin  *bufio.Writer
	stdout *bufio.Reader
	stderr *bufio.Reader
}

// Unpack unpacks the QEMU distribution.
func Unpack() (*Distribution, error) {
	ex, err := os.Executable()
	if err != nil {
		return nil, err
	}
	exPath := filepath.Dir(ex)
	archivePath := filepath.Join(exPath, "test_data/qemu/qemu.tar.gz")

	unpackedPath, err := ioutil.TempDir("", "qemu-distro")
	if err != nil {
		return nil, err
	}

	err = untar(unpackedPath, archivePath)
	if err != nil {
		os.RemoveAll(unpackedPath)
		return nil, err
	}

	return &Distribution{exPath: exPath, unpackedPath: unpackedPath}, nil
}

// Delete removes the QEMU-related artifacts.
func (d *Distribution) Delete() {
	os.RemoveAll(d.unpackedPath)
}

func (d *Distribution) systemPath(arch Arch) string {
	switch arch {
	case X64:
		return filepath.Join(d.unpackedPath, "bin/qemu-system-x86_64")
	case Arm64:
		return filepath.Join(d.unpackedPath, "bin/qemu-system-aarch64")
	}
	return ""
}

func (d *Distribution) kernelPath(arch Arch) string {
	switch arch {
	case X64:
		return filepath.Join(d.exPath, "test_data/qemu/multiboot.bin")
	case Arm64:
		return filepath.Join(d.exPath, "test_data/qemu/qemu-boot-shim.bin")
	}
	return ""
}

// TargetCPU returs the target CPU used by the build that produced this library.
func (d *Distribution) TargetCPU() (Arch, error) {
	path := filepath.Join(d.exPath, "test_data/qemu/target_cpu.txt")
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return X64, err
	}
	name := string(bytes)
	switch name {
	case "x64":
		return X64, nil
	case "arm64":
		return Arm64, nil
	}
	return X64, fmt.Errorf("unknown target CPU: %s", name)
}

// Create creates an instance of QEMU with the given parameters.
func (d *Distribution) Create(params Params) *Instance {
	path := d.systemPath(params.Arch)
	args := []string{}
	args = append(args, "-kernel", d.kernelPath(params.Arch), "-initrd", params.ZBI)
	args = append(args, "-m", "2048", "-nographic", "-net", "none", "-smp", "4,threads=2",
		"-machine", "q35", "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
		"-cpu", "Haswell,+smap,-check,-fsgsbase")
	cmdline := "kernel.serial=legacy kernel.entropy-mixin=1420bb81dc0396b37cc2d0aa31bb2785dadaf9473d0780ecee1751afb5867564 kernel.halt-on-panic=true"
	args = append(args, "-append", cmdline)
	return &Instance{
		cmd: exec.Command(path, args...),
	}
}

// Start the QEMU instance.
func (i *Instance) Start() error {
	stdin, err := i.cmd.StdinPipe()
	if err != nil {
		return err
	}
	stdout, err := i.cmd.StdoutPipe()
	if err != nil {
		return err
	}
	stderr, err := i.cmd.StderrPipe()
	if err != nil {
		return err
	}
	i.stdin = bufio.NewWriter(stdin)
	i.stdout = bufio.NewReader(stdout)
	i.stderr = bufio.NewReader(stderr)
	return i.cmd.Start()
}

// Kill terminates the QEMU instance.
func (i *Instance) Kill() error {
	return i.cmd.Process.Kill()
}

// RunCommand runs the given command in the serial console for the QEMU instance.
func (i *Instance) RunCommand(cmd string) error {
	_, err := i.stdin.WriteString(fmt.Sprintf("%s\n", cmd))
	return err
}

// WaitForLogMessage reads log messages from the QEMU instance until it reads
// a message that contains the given string.
func (i *Instance) WaitForLogMessage(msg string) error {
	for {
		line, err := i.stdout.ReadString('\n')
		if err != nil {
			return err
		}
		if strings.Contains(line, msg) {
			return nil
		}
	}
}
