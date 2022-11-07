// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package flasher

import (
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"golang.org/x/crypto/ssh"
)

type BuildFlasher struct {
	FfxToolPath   string
	FlashManifest string
	usePB         bool
	sshPublicKey  ssh.PublicKey
	stdout        io.Writer
	target        string
}

type Flasher interface {
	Flash(ctx context.Context) error
	SetTarget(target string)
}

// NewBuildFlasher constructs a new flasher that uses `ffxPath` as the path
// to the tool used to flash a device using flash.json located
// at `flashManifest`. Also accepts a number of optional parameters.
func NewBuildFlasher(ffxPath, flashManifest string, usePB bool, options ...BuildFlasherOption) (*BuildFlasher, error) {
	p := &BuildFlasher{
		FfxToolPath:   ffxPath,
		FlashManifest: flashManifest,
		usePB:         usePB,
	}

	for _, opt := range options {
		if err := opt(p); err != nil {
			return nil, err
		}
	}

	return p, nil
}

type BuildFlasherOption func(p *BuildFlasher) error

// Sets the SSH public key that the Flasher will bake into the device as an
// authorized key.
func SSHPublicKey(publicKey ssh.PublicKey) BuildFlasherOption {
	return func(p *BuildFlasher) error {
		p.sshPublicKey = publicKey
		return nil
	}
}

// Send stdout from the ffx target flash scripts to `writer`. Defaults to the parent
// stdout.
func Stdout(writer io.Writer) BuildFlasherOption {
	return func(p *BuildFlasher) error {
		p.stdout = writer
		return nil
	}
}

// SetTarget sets the target to flash.
func (p *BuildFlasher) SetTarget(target string) {
	p.target = target
}

// Flash a device with flash.json manifest.
func (p *BuildFlasher) Flash(ctx context.Context) error {
	flasherArgs := []string{}

	// Write out the public key's authorized keys.
	if p.sshPublicKey != nil {
		authorizedKeys, err := os.CreateTemp("", "")
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

		flasherArgs = append(flasherArgs, "--authorized-keys", authorizedKeys.Name())
	}
	if p.usePB {
		flasherArgs = append(flasherArgs, "--product-bundle")
	}
	return p.runFlash(ctx, flasherArgs...)
}

func (p *BuildFlasher) runFlash(ctx context.Context, args ...string) error {
	var finalArgs []string
	if p.target != "" {
		finalArgs = []string{"--target", p.target}
	}
	finalArgs = append(finalArgs, []string{"target", "flash"}...)
	finalArgs = append(finalArgs, args...)
	finalArgs = append(finalArgs, p.FlashManifest)

	path, err := exec.LookPath(p.FfxToolPath)
	if err != nil {
		return err
	}

	logger.Infof(ctx, "running: %s %q", path, finalArgs)
	cmd := exec.CommandContext(ctx, path, finalArgs...)
	if p.stdout != nil {
		cmd.Stdout = p.stdout
	} else {
		cmd.Stdout = os.Stdout
	}
	cmd.Stderr = os.Stderr

	// Spawn a new FFX isolate dir and add it to the flash command's env.
	isoDir, err := os.MkdirTemp("", "systemTestIsoDir*")
	if err != nil {
		return err
	}
	defer os.RemoveAll(isoDir)
	cmd.Env = os.Environ()
	cmd.Env = append(cmd.Env, fmt.Sprintf("FFX_ISOLATE_DIR=%s", isoDir))

	cmdRet := cmd.Run()
	logger.Infof(ctx, "finished running %s %q: %q", path, finalArgs, cmdRet)
	return cmdRet
}
