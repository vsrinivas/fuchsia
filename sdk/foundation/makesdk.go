///bin/true ; exec /usr/bin/env go run "$0" "$@"
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/tar"
	"bufio"
	"compress/gzip"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"runtime"
	"strings"
)

var archive = flag.Bool("archive", true, "Whether to archive the output")
var output = flag.String("output", "fuchsia-sdk.tgz", "Name of the archive")
var outDir = flag.String("out-dir", "", "Output directory")
var toolchain = flag.Bool("toolchain", false, "Include toolchain")
var toolchainLibs = flag.Bool("toolchain-lib", true, "Include toolchain libraries in SDK. Typically used when --toolchain is false")
var sysroot = flag.Bool("sysroot", true, "Include sysroot")
var kernelImg = flag.Bool("kernel-img", true, "Include kernel image")
var kernelDebugObjs = flag.Bool("kernel-dbg", true, "Include kernel objects with debug symbols")
var bootdata = flag.Bool("bootdata", true, "Include bootdata")
var qemu = flag.Bool("qemu", true, "Include QEMU binary")
var tools = flag.Bool("tools", true, "Include additional tools")
var verbose = flag.Bool("v", false, "Verbose output")
var dryRun = flag.Bool("n", false, "Dry run - print what would happen but don't actually do it")

type compType int

const (
	dirType compType = iota
	fileType
	customType
)

const x64BuildDir = "out/release-x64"
const armBuildDir = "out/release-arm64"

type component struct {
	flag      *bool  // Flag controlling whether this component should be included
	srcPrefix string // Source path prefix relative to the fuchsia root
	dstPrefix string // Destination path prefix relative to the SDK root
	t         compType
	f         func(src, dst string) error // When t is 'custom', function to run to copy
}

type dir struct {
	flag     *bool
	src, dst string
}

type file struct {
	flag     *bool
	src, dst string
}

var (
	hostOs      string
	hostCpu     string
	components  []component
	privateDirs []string
)

func init() {
	hostCpu = runtime.GOARCH
	if hostCpu == "amd64" {
		hostCpu = "x64"
	}
	hostOs = runtime.GOOS
	if hostOs == "darwin" {
		hostOs = "mac"
	}

	zxBuildDir := "out/build-zircon"
	x64ZxBuildDir := path.Join(zxBuildDir, "build-x64")
	armZxBuildDir := path.Join(zxBuildDir, "build-arm64")
	qemuDir := fmt.Sprintf("buildtools/%s-%s/qemu/", hostOs, hostCpu)

	dirs := []dir{
		{
			qemu,
			qemuDir,
			"qemu",
		},
		{
			tools,
			"out/build-zircon/tools",
			"tools",
		},
		{
			toolchain,
			fmt.Sprintf("buildtools/%s-%s/clang", hostOs, hostCpu),
			"clang",
		},
		{
			// TODO(https://crbug.com/724204): Remove this once Chromium starts using upstream compiler-rt builtins.
			toolchainLibs,
			fmt.Sprintf("buildtools/%s-%s/clang/lib/clang/7.0.0/lib/fuchsia", hostOs, hostCpu),
			"toolchain_libs/clang/7.0.0/lib/fuchsia",
		},
	}

	files := []file{
		{
			kernelImg,
			"out/build-zircon/build-arm64/qemu-zircon.bin",
			"target/aarch64/zircon.bin",
		},
		{
			kernelImg,
			path.Join(armBuildDir, "bootdata-blob-qemu.bin"),
			"target/aarch64/bootdata-blob.bin",
		},
		{
			kernelImg,
			path.Join(armBuildDir, "images/fvm.blk"),
			"target/aarch64/fvm.blk",
		},

		{
			kernelImg,
			"out/build-zircon/build-x64/zircon.bin",
			"target/x86_64/zircon.bin",
		},
		{
			kernelImg,
			path.Join(x64BuildDir, "bootdata-blob-pc.bin"),
			"target/x86_64/bootdata-blob.bin",
		},
		{
			kernelImg,
			path.Join(x64BuildDir, "images/local-pc.esp.blk"),
			"target/x86_64/local.esp.blk",
		},
		{
			kernelImg,
			path.Join(x64BuildDir, "images/zircon-pc.vboot"),
			"target/x86_64/zircon.vboot",
		},
		{
			kernelImg,
			path.Join(x64BuildDir, "images/fvm.blk"),
			"target/x86_64/fvm.blk",
		},
		{
			kernelImg,
			path.Join(x64BuildDir, "images/fvm.sparse.blk"),
			"target/x86_64/fvm.sparse.blk",
		},
		{
			sysroot,
			path.Join(x64BuildDir, "obj/build/images/system_image.manifest.stripped/lib/libc++.so.2"),
			"arch/x64/dist/libc++.so.2",
		},
		{
			sysroot,
			path.Join(armBuildDir, "obj/build/images/system_image.manifest.stripped/lib/libc++.so.2"),
			"arch/arm64/dist/libc++.so.2",
		},
		{
			sysroot,
			path.Join(x64BuildDir, "obj/build/images/system_image.manifest.stripped/lib/libc++abi.so.1"),
			"arch/x64/dist/libc++abi.so.1",
		},
		{
			sysroot,
			path.Join(armBuildDir, "obj/build/images/system_image.manifest.stripped/lib/libc++abi.so.1"),
			"arch/arm64/dist/libc++abi.so.1",
		},
		{
			sysroot,
			path.Join(x64BuildDir, "obj/build/images/system_image.manifest.stripped/lib/libunwind.so.1"),
			"arch/x64/dist/libunwind.so.1",
		},
		{
			sysroot,
			path.Join(armBuildDir, "obj/build/images/system_image.manifest.stripped/lib/libunwind.so.1"),
			"arch/arm64/dist/libunwind.so.1",
		},
		{
			tools,
			path.Join(x64BuildDir, "host_x64/far"),
			"tools/far",
		},
		{
			tools,
			path.Join(x64BuildDir, "host_x64/pm"),
			"tools/pm",
		},
	}

	components = []component{
		{
			kernelDebugObjs,
			x64ZxBuildDir,
			"sysroot/x86_64-fuchsia/debug",
			customType,
			copyKernelDebugObjs,
		},
		{
			kernelDebugObjs,
			x64BuildDir,
			"sysroot/x86_64-fuchsia/debug",
			customType,
			copyIdsTxt,
		},
		{
			kernelDebugObjs,
			armZxBuildDir,
			"sysroot/aarch64-fuchsia/debug",
			customType,
			copyKernelDebugObjs,
		},
		{
			kernelDebugObjs,
			armBuildDir,
			"sysroot/aarch64-fuchsia/debug",
			customType,
			copyIdsTxt,
		},
	}
	for _, d := range dirs {
		components = append(components, component{d.flag, d.src, d.dst, dirType, nil})
	}
	for _, f := range files {
		components = append(components, component{f.flag, f.src, f.dst, fileType, nil})
	}
	privateDirs = []string{
		"arch/arm64/sysroot/include/zircon/device",
		"arch/x64/sysroot/include/zircon/device",
		"sysroot/aarch64-fuchsia/include/zircon/device",
		"sysroot/x86_64-fuchsia/include/zircon/device",
	}
}

func createLayout(manifest, fuchsiaRoot, outDir string) {
	for idx, buildDir := range []string{x64BuildDir, armBuildDir} {
		manifestPath := filepath.Join(fuchsiaRoot, buildDir, "sdk-manifests", manifest)
		cmd := filepath.Join(fuchsiaRoot, "scripts", "sdk", "foundation", "generate.py")
		args := []string{"--manifest", manifestPath, "--output", outDir}
		if idx > 0 {
			args = append(args, "--overlay")
		}
		if *verbose || *dryRun {
			fmt.Println("createLayout", cmd, args)
		}
		if *dryRun {
			return
		}
		out, err := exec.Command(cmd, args...).CombinedOutput()
		if err != nil {
			log.Fatal("create_layout.py failed with output", string(out), "error", err)
		}
	}
}

func copyKernelDebugObjs(src, dstPrefix string) error {
	// The kernel debug information lives in many .elf files in the out directory
	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if !info.IsDir() && filepath.Ext(path) == ".elf" {
			if err := copyFile(path, filepath.Join(dstPrefix, path[len(src):])); err != nil {
				return err
			}
		}
		return nil
	})
}

func copyIdsTxt(src, dstPrefix string) error {
	// The ids.txt file has absolute paths but relative paths within the SDK are
	// more useful to users.
	srcIds, err := os.Open(filepath.Join(src, "ids.txt"))
	if err != nil {
		return fmt.Errorf("could not open ids.txt", err)
	}
	defer srcIds.Close()
	if *dryRun {
		return nil
	}
	dstIds, err := os.Create(filepath.Join(dstPrefix, "ids.txt"))
	if err != nil {
		return fmt.Errorf("could not create ids.txt", err)
	}
	defer dstIds.Close()
	scanner := bufio.NewScanner(srcIds)
	cwd, _ := os.Getwd()
	absBase := filepath.Join(cwd, src)
	for scanner.Scan() {
		s := strings.Split(scanner.Text(), " ")
		id, absPath := s[0], s[1]
		relPath, err := filepath.Rel(absBase, absPath)
		if err != nil {
			log.Println("could not create relative path from absolute path", absPath, "and base", absBase, "skipping entry")
		} else {
			fmt.Fprintln(dstIds, id, relPath)
		}
	}
	return nil
}

func copyFile(src, dst string) error {
	if *dryRun {
		return nil
	}

	if err := os.MkdirAll(filepath.Dir(dst), os.ModePerm); err != nil {
		return err
	}

	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	fi, err := in.Stat()
	if err != nil {
		return err
	}

	out, err := os.OpenFile(dst, os.O_RDWR|os.O_CREATE, fi.Mode())
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	if err != nil {
		return err
	}

	return nil
}

func copyDir(src, dst string) error {
	if *dryRun {
		return nil
	}

	if err := os.MkdirAll(dst, os.ModePerm); err != nil {
		return err
	}

	infos, err := ioutil.ReadDir(src)
	if err != nil {
		return err
	}

	for _, info := range infos {
		var err error
		if info.IsDir() {
			err = copyDir(filepath.Join(src, info.Name()), filepath.Join(dst, info.Name()))

		} else {
			err = copyFile(filepath.Join(src, info.Name()), filepath.Join(dst, info.Name()))
		}
		if err != nil {
			return err
		}
	}

	return nil
}

func createTar(src, dst string) error {
	if *verbose || *dryRun {
		fmt.Println("Archiving", src, "to", dst)
	}
	if *dryRun {
		return nil
	}

	file, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer file.Close()

	gw := gzip.NewWriter(file)
	defer gw.Close()
	tw := tar.NewWriter(gw)
	defer tw.Close()

	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		header, err := tar.FileInfoHeader(info, info.Name())
		if err != nil {
			return err
		}

		name, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}
		header.Name = name
		if err := tw.WriteHeader(header); err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}

		file, err := os.Open(path)
		if err != nil {
			return err
		}
		defer file.Close()
		_, err = io.Copy(tw, file)
		return err
	})
}

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `Usage: ./makesdk.go [flags] /path/to/fuchsia/root

This script creates a Fuchsia SDK containing the specified features and places it into a tarball.

To use, first build a release mode Fuchsia build with the 'runtime' module configured as the
only module.
`)
		flag.PrintDefaults()
	}
	flag.Parse()
	fuchsiaRoot := flag.Arg(0)
	if _, err := os.Stat(fuchsiaRoot); os.IsNotExist(err) {
		flag.Usage()
		log.Fatalf("Fuchsia root not found at \"%v\"\n", fuchsiaRoot)
	}
	if *outDir == "" {
		var err error
		*outDir, err = ioutil.TempDir("", "fuchsia-sdk")
		if err != nil {
			log.Fatal("Could not create temporary directory: ", err)
		}
		defer os.RemoveAll(*outDir)
	} else if _, err := os.Stat(*outDir); os.IsNotExist(err) {
		if err := os.MkdirAll(*outDir, os.ModePerm); err != nil {
			log.Fatalf("Could not create directory %s: %v", *outDir, err)
		}
	}

	createLayout("garnet", fuchsiaRoot, *outDir)

	for _, c := range components {
		if *c.flag {
			src := filepath.Join(fuchsiaRoot, c.srcPrefix)
			dst := filepath.Join(*outDir, c.dstPrefix)
			switch c.t {
			case dirType:
				if err := copyDir(src, dst); err != nil {
					log.Fatalf("failed to copy directory %s to %s: %v", src, dst, err)
				}
			case fileType:
				if err := copyFile(src, dst); err != nil {
					log.Fatalf("failed to copy file %s to %s: %v", src, dst, err)
				}
			case customType:
				if err := c.f(src, dst); err != nil {
					log.Fatalf("failed to copy %s to %s: %v", src, dst, err)
				}
			}
		}
	}
	for _, d := range privateDirs {
		os.RemoveAll(filepath.Join(*outDir, d))
	}
	if *archive {
		if err := createTar(*outDir, *output); err != nil {
			log.Fatalf("failed to compress %s: %v", *outDir, err)
		}
	}
}
