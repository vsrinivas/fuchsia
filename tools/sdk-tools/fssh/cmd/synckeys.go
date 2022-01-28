// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"
	"fmt"
	"os"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/fssh/synckeys"

	"github.com/google/subcommands"
)

const remoteHostFlag = "remote-host"

type syncKeysCmd struct {
	remoteHost string
	logLevel   logger.LogLevel
}

func (*syncKeysCmd) Name() string { return "sync-keys" }

func (*syncKeysCmd) Synopsis() string {
	return "Sync SSH key files associated with Fuchsia between a local and remote workstation."
}

func (*syncKeysCmd) Usage() string {
	return fmt.Sprintf(`fssh sync-keys [-%s remote-host]
Sync SSH key files associated with Fuchsia between a local and remote workstation. If no SSH key files associated with Fuchsia are found Fuchsia key files are generated locally and copied to the remote.
Inspects the SSH private/public key pair and authorized keys file in $HOME/.ssh/fuchsia_*. These files are used by all Fuchsia development tools to access target devices.
`, remoteHostFlag)
}

func (c *syncKeysCmd) SetFlags(f *flag.FlagSet) {
	c.logLevel = logger.InfoLevel // Default that may be overridden.
	f.StringVar(&c.remoteHost, remoteHostFlag, "", "The remote host where development is taking place.")
	f.Var(&c.logLevel, logLevelFlag, "Output verbosity, can be fatal, error, warning, info, debug or trace.")
}

func (c *syncKeysCmd) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	log := logger.NewLogger(c.logLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "synckeys ")
	log.SetFlags(logFlags)

	ctx = logger.WithLogger(ctx, log)
	if c.remoteHost == "" {
		log.Fatalf("Please set the -%s flag to sync keys.", remoteHostFlag)
	}

	log.Infof("Syncing Fuchsia SSH keys between this machine and %q...", c.remoteHost)

	if err := synckeys.Fuchsia(ctx, c.remoteHost); err != nil {
		log.Fatalf("Unable to sync Fuchsia keys: %v", err)
	}

	log.Infof("Sync completed successfully.")
	return subcommands.ExitSuccess
}
