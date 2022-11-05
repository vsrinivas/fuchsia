// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"

	botanistconstants "go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner"
)

func usage() {
	fmt.Printf(`testrunner [flags] tests-file

Executes all tests found in the JSON [tests-file]
Fuchsia tests require both the node address of the fuchsia instance and a private
SSH key corresponding to a authorized key to be set in the environment under
%s and %s respectively.
`, botanistconstants.DeviceAddrEnvKey, botanistconstants.SSHKeyEnvKey)
}

func main() {
	var flags testrunner.TestrunnerFlags
	flags.LogLevel = logger.InfoLevel // Default that may be overridden.

	flag.BoolVar(&flags.Help, "help", false, "Whether to show Usage and exit.")
	flag.StringVar(&flags.OutDir, "out-dir", "", "Optional path where a directory containing test results should be created.")
	flag.StringVar(&flags.NsjailPath, "nsjail", "", "Optional path to an NsJail binary to use for linux host test sandboxing.")
	flag.StringVar(&flags.NsjailRoot, "nsjail-root", "", "Path to the directory to use as the NsJail root directory")
	flag.StringVar(&flags.LocalWD, "C", "", "Working directory of local testing subprocesses; if unset the current working directory will be used.")
	flag.BoolVar(&flags.UseRuntests, "use-runtests", false, "Whether to default to running fuchsia tests with runtests; if false, run_test_component will be used.")
	flag.StringVar(&flags.SnapshotFile, "snapshot-output", "", "The output filename for the snapshot. This will be created in the output directory.")
	flag.Var(&flags.LogLevel, "level", "Output verbosity, can be fatal, error, warning, info, debug or trace.")
	flag.StringVar(&flags.FfxPath, "ffx", "", "Path to the ffx tool.")
	flag.IntVar(&flags.FfxExperimentLevel, "ffx-experiment-level", 0, "The level of experimental features to enable. If -ffx is not set, this will have no effect.")
	flag.BoolVar(&flags.PrefetchPackages, "prefetch-packages", false, "Prefetch any test packages in the background.")
	flag.BoolVar(&flags.UseSerial, "use-serial", false, "Use serial to run tests on the target.")

	flag.Usage = usage
	flag.Parse()

	if flags.Help || flag.NArg() != 1 {
		flag.Usage()
		flag.PrintDefaults()
		return
	}

	const logFlags = log.Ltime | log.Lmicroseconds | log.Lshortfile

	log := logger.NewLogger(flags.LogLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "testrunner ")
	log.SetFlags(logFlags)
	ctx := logger.WithLogger(context.Background(), log)
	ctx, cancel := signal.NotifyContext(ctx, syscall.SIGTERM, syscall.SIGINT)
	defer cancel()

	if err := testrunner.SetupAndExecute(ctx, flags, flag.Arg(0)); err != nil {
		logger.Fatalf(ctx, err.Error())
	}
}
