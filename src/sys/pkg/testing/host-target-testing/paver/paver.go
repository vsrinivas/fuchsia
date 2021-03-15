// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package paver

import (
	"context"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"golang.org/x/crypto/ssh"
)

const ImageManifest = "images.json"

type BuildPaver struct {
	BootserverPath  string
	ImageDir        string
	sshPublicKey    ssh.PublicKey
	overrideVBMetaA *string
	overrideZirconA *string
	stdout          io.Writer
}

type Paver interface {
	Pave(ctx context.Context, deviceName string) error
}

// NewBuildPaver constructs a new paver that uses `bootserverPath` as the path
// to the tool used to pave Zedboot and Fuchsia with the image manifest located
// in `imageDir`. Also accepts a number of optional parameters.
func NewBuildPaver(bootserverPath, imageDir string, options ...BuildPaverOption) (*BuildPaver, error) {
	p := &BuildPaver{
		BootserverPath: bootserverPath,
		ImageDir:       imageDir,
	}

	for _, opt := range options {
		if err := opt(p); err != nil {
			return nil, err
		}
	}

	return p, nil
}

type BuildPaverOption func(p *BuildPaver) error

// Sets the SSH public key that the Paver will bake into the device as an
// authorized key.
func SSHPublicKey(publicKey ssh.PublicKey) BuildPaverOption {
	return func(p *BuildPaver) error {
		p.sshPublicKey = publicKey
		return nil
	}
}

// Sets a path to an image that the Paver will use to override the ZIRCON-A ZBI.
func OverrideSlotA(imgPath string) BuildPaverOption {
	return func(p *BuildPaver) error {
		if _, err := os.Stat(imgPath); err != nil {
			return err
		}
		p.overrideZirconA = &imgPath
		return nil
	}
}

// Sets the paths to the images that the Paver will use to override vbmeta_a.
func OverrideVBMetaA(vbmetaPath string) BuildPaverOption {
	return func(p *BuildPaver) error {
		if _, err := os.Stat(vbmetaPath); err != nil {
			return err
		}
		p.overrideVBMetaA = &vbmetaPath
		return nil
	}
}

// Send stdout from the paver scripts to `writer`. Defaults to the parent
// stdout.
func Stdout(writer io.Writer) BuildPaverOption {
	return func(p *BuildPaver) error {
		p.stdout = writer
		return nil
	}
}

// Pave runs a paver service for one pave. If `deviceName` is not empty, the
// pave will only be applied to the specified device.
func (p *BuildPaver) Pave(ctx context.Context, deviceName string) error {
	paverArgs := []string{"--fail-fast-if-version-mismatch"}

	// Write out the public key's authorized keys.
	if p.sshPublicKey != nil {
		authorizedKeys, err := ioutil.TempFile("", "")
		if err != nil {
			return err
		}
		defer os.Remove(authorizedKeys.Name())

		if _, err := authorizedKeys.Write(ssh.MarshalAuthorizedKey(p.sshPublicKey)); err != nil {
			return err
		}

		if err := authorizedKeys.Close(); err != nil {
			return err
		}

		paverArgs = append(paverArgs, "--authorized-keys", authorizedKeys.Name())
	}

	if p.overrideZirconA != nil {
		paverArgs = append(paverArgs, "--zircona", *p.overrideZirconA)
	}

	if p.overrideVBMetaA != nil {
		paverArgs = append(paverArgs, "--vbmetaa", *p.overrideVBMetaA)
	}

	// Run bootserver with pave-zedboot mode to bootstrap the new bootloader and zedboot.
	if err := p.runPave(ctx, deviceName, "--mode", "pave-zedboot", "--allow-zedboot-version-mismatch"); err != nil {
		return err
	}

	// Run bootserver with pave mode to install Fuchsia.
	paverArgs = append([]string{"--mode", "pave"}, paverArgs...)
	return p.runPave(ctx, deviceName, paverArgs...)
}

func (p *BuildPaver) runPave(ctx context.Context, deviceName string, args ...string) error {
	args = append([]string{"--images", filepath.Join(p.ImageDir, ImageManifest)}, args...)

	logger.Infof(ctx, "paving device %q with %s %v", deviceName, p.BootserverPath, args)
	path, err := exec.LookPath(p.BootserverPath)
	if err != nil {
		return err
	}

	args = append(args, "-1")
	if deviceName != "" {
		args = append(args, "-n", deviceName)
	}
	logger.Infof(ctx, "running: %s %q", path, args)
	cmd := exec.CommandContext(ctx, path, args...)
	if p.stdout != nil {
		cmd.Stdout = p.stdout
	} else {
		cmd.Stdout = os.Stdout
	}
	cmd.Stderr = os.Stderr
	return cmd.Run()
}
