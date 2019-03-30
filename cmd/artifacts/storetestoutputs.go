// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"sync"

	"fuchsia.googlesource.com/tools/artifacts"
	"github.com/google/subcommands"
	"go.chromium.org/luci/auth"
	"go.chromium.org/luci/auth/client/authcli"
	"go.chromium.org/luci/hardcoded/chromeinfra"
)

const (
	// The maximum number of concurrent uploads. We rate limit this because we don't know
	// how many entries there are in the input manifest file, so we don't know how many
	// go-routines will be kicked off during execution. At around 100 or so concurrent
	// uploads, the Golang storage API starts returning 400 errors and files fail to flush
	// (when writer.Close() is called). Tweak this number as needed.
	maxConcurrentUploads = 100
)

// TestOutputsManifest describes how to upload test files. This is intentionally written
// to match the schema of Fuchsia's existing summary.json "tests" field for backward
// compatibility. We should migrate away from using a single output file for stdout and
// stderr and prefer uploading separate files.
type TestOutputsManifest = []TestOutputs
type TestOutputs struct {
	Name       string `json:"name"`
	OutputFile string `json:"output_file"`
}

// StoreTestOutputsCommand performs a batch upload of test outputs to Cloud Storage.
type StoreTestOutputsCommand struct {
	authFlags authcli.Flags
	bucket  string
	build   string
	testEnv string
	workers sync.WaitGroup
}

func (*StoreTestOutputsCommand) Name() string {
	return "storetestoutputs"
}

func (*StoreTestOutputsCommand) Usage() string {
	return fmt.Sprintf(`storetestoutputs [flags] outputs.json

The input manifest is a JSON list of objects with the following scheme:

	{
	  "name": "The name of the test",
	  "output_file": "/path/to/test/output/file"
	}

output_file is written to Cloud Storage as just %q within the hierarchy documented in
//tools/artifacts/doc.go.`, artifacts.DefaultTestOutputName)
}

func (*StoreTestOutputsCommand) Synopsis() string {
	return fmt.Sprintf("stores test output files in Cloud Storage")
}

func (cmd *StoreTestOutputsCommand) SetFlags(f *flag.FlagSet) {
	cmd.authFlags.Register(flag.CommandLine, chromeinfra.DefaultAuthOptions())
	f.StringVar(&cmd.bucket, "bucket", "", "The Cloud Storage bucket to write to")
	f.StringVar(&cmd.build, "build", "", "The BuildBucket build ID")
	f.StringVar(&cmd.testEnv, "testenv", "", "A canonical name for the test environment")
}

func (cmd *StoreTestOutputsCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	opts, err := cmd.authFlags.Options()
	if err != nil {
		log.Println(err)
		return subcommands.ExitFailure
	}
	if err := cmd.validateFlags(f); err != nil {
		log.Println(err)
		return subcommands.ExitFailure
	}
	manifest, err := cmd.parseManifestFile(f.Arg(0))
	if err != nil {
		log.Println(err)
		return subcommands.ExitFailure
	}
	if !cmd.execute(ctx, manifest, opts) {
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

func (cmd *StoreTestOutputsCommand) parseManifestFile(path string) (TestOutputsManifest, error) {
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q: %v", path, err)
	}
	var manifest TestOutputsManifest
	if err := json.Unmarshal(bytes, &manifest); err != nil {
		return nil, fmt.Errorf("fail to unmarshal manifest: %v", err)
	}
	return manifest, nil
}

func (cmd *StoreTestOutputsCommand) validateFlags(f *flag.FlagSet) error {
	if f.NArg() != 1 {
		return fmt.Errorf("expect exactly 1 positional argument")
	}
	if cmd.bucket == "" {
		return fmt.Errorf("missing -bucket")
	}
	if cmd.build == "" {
		return fmt.Errorf("missing -build")
	}
	if cmd.testEnv == "" {
		return fmt.Errorf("missing -testenv")
	}
	return nil
}

// execute spawns a worker pool to perform the upload. The pool is limited in size by
// maxConcurrentUploads to avoid load failures in the storage API.
func (cmd *StoreTestOutputsCommand) execute(ctx context.Context, manifest TestOutputsManifest, opts auth.Options) bool {
	const workerCount = maxConcurrentUploads
	success := true
	outputs := make(chan TestOutputs)
	errs := make(chan error, workerCount)
	for i := 0; i < workerCount; i++ {
		cmd.workers.Add(1)
		go cmd.worker(ctx, &cmd.workers, outputs, errs, opts)
	}
	for _, entry := range manifest {
		outputs <- entry
	}
	close(outputs)
	go func() {
		for err := range errs {
			success = false
			log.Println(err)
		}
	}()
	cmd.workers.Wait()
	close(errs)
	return success
}

func (cmd *StoreTestOutputsCommand) worker(ctx context.Context, wg *sync.WaitGroup, outputs <-chan TestOutputs, errs chan<- error, opts auth.Options) {
	defer wg.Done()
	cli, err := artifacts.NewClient(ctx, opts)
	if err != nil {
		errs <- err
		return
	}
	dir := cli.GetBuildDir(cmd.bucket, cmd.build)
	for output := range outputs {
		if err := cmd.upload(context.Background(), output, dir); err != nil {
			errs <- err
		}
	}
}

func (cmd *StoreTestOutputsCommand) upload(ctx context.Context, outputs TestOutputs, dir *artifacts.BuildDirectory) error {
	fd, err := os.Open(outputs.OutputFile)
	if err != nil {
		return fmt.Errorf("failed to read %q: %v", outputs.OutputFile, err)
	}
	object := dir.NewTestOutputObject(ctx, outputs.Name, cmd.testEnv)
	writer := object.NewWriter(ctx)
	if _, err := io.Copy(writer, fd); err != nil {
		return fmt.Errorf("failed to write %q: %v", object.ObjectName(), err)
	}
	if err := writer.Close(); err != nil {
		return fmt.Errorf("failed to flush bufferfor %q: %v", object.ObjectName(), err)
	}
	return nil
}
