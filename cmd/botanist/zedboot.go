// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"archive/tar"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"os"
	"os/signal"
	"path"
	"strings"
	"syscall"
	"time"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/fastboot"
	"fuchsia.googlesource.com/tools/netboot"
	"fuchsia.googlesource.com/tools/pdu"
	"fuchsia.googlesource.com/tools/retry"
	"fuchsia.googlesource.com/tools/tftp"
	"github.com/google/subcommands"
)

// ZedbootCommand is a Command implementation for running the testing workflow on a device
// that boots with Zedboot.
type ZedbootCommand struct {
	// KernelImage is the path to a kernel image.
	kernelImage string

	// RamdiskImage is the path to a ramdisk image.
	ramdiskImage string

	// EfiImage is the path to an EFI image.
	efiImage string

	// KerncImage is the path to a kernc image.
	kerncImage string

	// FVMImages is a list of paths to sparse fvm images to be paved.
	fvmImages botanist.StringsFlag

	// ZedbootImage is the path to the zedboot image.
	zedbootImage string

	// PropertiesFile is the path to a file where deviceProperties have been written.
	propertiesFile string

	// TestResultsDir is the directory on target to where test results will be written.
	testResultsDir string

	// SummaryFilename is the name of the test summary JSON file to be written to
	// testResultsDir.
	summaryFilename string

	// FilePollInterval is the duration waited between checking for test summary file
	// on the target to be written.
	filePollInterval time.Duration

	// OutputArchive is a path on host to where the tarball containing the test results
	// will be output.
	outputArchive string

	// CmdlineFile is the path to a file of additional kernel command-line arguments.
	cmdlineFile string

	// FastbootTool is the path to the fastboot tool.
	fastbootTool string

	// Fastboot is true iff we will use first use fastboot to flash Zedboot and boot into Zedboot.
	fastboot bool
}

func (*ZedbootCommand) Name() string {
	return "zedboot"
}

func (*ZedbootCommand) Usage() string {
	return "zedboot [flags...] [kernel command-line arguments...]\n\nflags:\n"
}

func (*ZedbootCommand) Synopsis() string {
	return "boots a Zedboot device and collects test results"
}

func (cmd *ZedbootCommand) SetFlags(f *flag.FlagSet) {
	f.StringVar(&cmd.kernelImage, "kernel", "", "path to kernel image")
	f.StringVar(&cmd.ramdiskImage, "ramdisk", "", "path to ramdisk image")
	f.StringVar(&cmd.efiImage, "efi", "", "path to EFI image to be paved")
	f.StringVar(&cmd.kerncImage, "kernc", "", "path to kernc image to be paved")
	f.Var(&cmd.fvmImages, "fvm", "paths to sparse FVM images to be paved (may be specified up to 4 times)")
	f.StringVar(&cmd.zedbootImage, "zedboot", "", "path to zedboot image to be flashed. Must be set together with -fastboot")
	f.StringVar(&cmd.testResultsDir, "results-dir", "/test", "path on target to where test results will be written")
	f.StringVar(&cmd.outputArchive, "out", "output.tar", "path on host to output tarball of test results")
	f.StringVar(&cmd.summaryFilename, "summary-name", botanist.TestSummaryFilename, "name of the file in the test directory")
	f.DurationVar(&cmd.filePollInterval, "poll-interval", 1*time.Minute, "time between checking for summary.json on the target")

	f.StringVar(&cmd.propertiesFile, "properties", "/etc/botanist/config.json", "path to file of device properties")

	f.StringVar(&cmd.cmdlineFile, "cmdline-file", "", "path to a file containing additional kernel command-line arguments")
	f.StringVar(&cmd.fastbootTool, "fastboot-tool", "./fastboot/fastboot", "path to the fastboot tool.")
	f.BoolVar(&cmd.fastboot, "fastboot", false, "If set, -fastboot-tool will be used to put the device into zedboot before "+
		"doing anything else. Must be set together with -zedboot")
}

func (cmd *ZedbootCommand) validateImages() error {
	var errs []string
	existsIfSet := func(filename string) {
		if filename == "" {
			return
		}
		_, err := os.Stat(filename)
		if err != nil {
			errs = append(errs, fmt.Sprintf("failed to stat %s: %v", filename, err.Error()))
		}
	}

	if cmd.kernelImage == "" {
		errs = append(errs, "|kernelImage| must be set.")
	}
	existsIfSet(cmd.kernelImage)
	existsIfSet(cmd.ramdiskImage)
	existsIfSet(cmd.efiImage)
	existsIfSet(cmd.kerncImage)
	for _, image := range cmd.fvmImages {
		existsIfSet(image)
	}
	if cmd.fastboot {
		existsIfSet(cmd.zedbootImage)
		existsIfSet(cmd.fastbootTool)
	}

	if len(errs) > 0 {
		return errors.New(strings.Join(errs, "\n"))
	}
	return nil
}

func (cmd *ZedbootCommand) runTests(ctx context.Context, nodename string, cmdlineArgs []string) error {
	// Find the node address UDP address.
	n := netboot.NewClient(time.Second)

	var addr *net.UDPAddr
	var err error
	// We need to retry here because botanist might try to discover before
	// zedboot is fully ready, so the packet that's sent out doesn't result
	// in any reply. We don't need to wait between calls because Discover
	// already has a 1 minute timeout for reading a UDP packet from zedboot.
	err = retry.Retry(ctx, retry.WithMaxRetries(retry.NewConstantBackoff(time.Second), 60), func() error {
		addr, err = n.Discover(nodename, false)
		return err
	})
	if err != nil {
		return fmt.Errorf("cannot find node \"%s\": %v\n", nodename, err)
	}

	// Transfer kernel, ramdisk, and command line args onto the node.
	client := tftp.NewClient()
	tftpAddr := &net.UDPAddr{
		IP:   addr.IP,
		Port: tftp.ClientPort,
		Zone: addr.Zone,
	}

	// TFTPFiles is effectively an ordered map, so the order in which
	// its contents are added is the order in which they'll be transferred
	// later.
	var files botanist.TFTPFiles
	if cmd.ramdiskImage != "" {
		files.Set(botanist.RamdiskFilename, cmd.ramdiskImage)
	}
	if cmd.fvmImages != nil {
		files.Set(botanist.FVMFilename, cmd.fvmImages...)
	}
	if cmd.efiImage != "" {
		files.Set(botanist.EFIFilename, cmd.efiImage)
	}
	if cmd.kerncImage != "" {
		files.Set(botanist.KerncFilename, cmd.kerncImage)
	}
	if cmd.kernelImage != "" {
		files.Set(botanist.KernelFilename, cmd.kernelImage)
	}
	if err := files.Transfer(ctx, client, tftpAddr); err != nil {
		return fmt.Errorf("cannot transfer files: %v\n", err)
	}
	if err := botanist.TransferCmdlineArgs(client, tftpAddr, cmdlineArgs); err != nil {
		return fmt.Errorf("cannot transer command-line arguments: %v\n", cmdlineArgs)
	}

	// Set up log listener and dump kernel output to stdout.
	l, err := netboot.NewLogListener(nodename)
	if err != nil {
		return fmt.Errorf("cannot listen: %v\n", err)
	}
	go func() {
		defer l.Close()
		log.Printf("starting log listener\n")
		for {
			data, err := l.Listen()
			if err != nil {
				continue
			}
			fmt.Print(data)
			select {
			case <-ctx.Done():
				return
			default:
			}
		}
	}()

	log.Printf("sending boot command\n")

	// Boot Fuchsia.
	if err := n.Boot(addr); err != nil {
		return fmt.Errorf("cannot boot: %v\n", err)
	}

	log.Printf("waiting for \"%s\"\n", cmd.summaryFilename)

	// Poll for summary.json; this relies on runtest being executed using
	// autorun and it eventually producing the summary.json file.
	var buffer bytes.Buffer
	var writer io.WriterTo
	err = retry.Retry(ctx, retry.NewConstantBackoff(cmd.filePollInterval), func() error {
		writer, err = client.Receive(tftpAddr, path.Join(cmd.testResultsDir, cmd.summaryFilename))
		return err
	})
	if err != nil {
		return fmt.Errorf("timed out waiting for tests to complete: %v", err)
	}

	log.Printf("reading \"%s\"\n", cmd.summaryFilename)

	if _, err := writer.WriteTo(&buffer); err != nil {
		return fmt.Errorf("failed to receive summary file: %v\n", err)
	}

	// Parse and save the summary.json file.
	var result botanist.TestSummary
	if err := json.Unmarshal(buffer.Bytes(), &result); err != nil {
		return fmt.Errorf("cannot unmarshall test results: %v\n", err)
	}

	file, err := os.OpenFile(cmd.outputArchive, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return fmt.Errorf("failed to create file %s: %v\n", cmd.outputArchive, err)
	}
	tw := tar.NewWriter(file)
	defer tw.Close()

	hdr := &tar.Header{
		Name: cmd.summaryFilename,
		Size: int64(buffer.Len()),
		Mode: 0666,
	}
	if err := tw.WriteHeader(hdr); err != nil {
		return fmt.Errorf("failed to write summary header: %v\n", err)
	}
	if _, err := tw.Write(buffer.Bytes()); err != nil {
		return fmt.Errorf("failed to write summary content: %v\n", err)
		log.Fatal(err)
	}
	log.Printf("copying test output\n")

	// Tar in a subroutine while busy-printing so that we do not hit an i/o timeout when
	// dealing with large files.
	c := make(chan error)
	go func() {
		// Copy test output from the node.
		for _, output := range result.Outputs {
			if err = botanist.WriteFileToTar(client, tftpAddr, tw, cmd.testResultsDir, output); err != nil {
				c <- err
				return
			}
		}
		for _, test := range result.Tests {
			if err = botanist.WriteFileToTar(client, tftpAddr, tw, cmd.testResultsDir, test.OutputFile); err != nil {
				c <- err
				return
			}
			// Copy data sinks if any are present.
			for _, sinks := range test.DataSinks {
				for _, sink := range sinks {
					if err = botanist.WriteFileToTar(client, tftpAddr, tw, cmd.testResultsDir, sink.File); err != nil {
						c <- err
						return
					}
				}
			}
		}
		c <- nil
	}()

	log.Printf("tarring test output...")
	ticker := time.NewTicker(5 * time.Second)
	for {
		select {
		case err := <-c:
			ticker.Stop()
			return err
		case <-ticker.C:
			log.Printf("...")
		}
	}
}

func (cmd *ZedbootCommand) fastbootToZedboot(ctx context.Context) error {
	// If it can't find any fastboot device, the fastboot tool will hang waiting, so we add a timeout.
	// All fastboot operations take less than a second on a developer workstation, so a
	// minute per each operation is very generous.
	ctx, _ = context.WithTimeout(ctx, 2*time.Minute)
	f := fastboot.Fastboot{cmd.fastbootTool}
	log.Printf("fastboot flashing zedboot image")
	if _, err := f.Flash(ctx, "boot", cmd.zedbootImage); err != nil {
		return fmt.Errorf("failed to flash the fastboot device: %v", err)
	}
	log.Printf("continuing from fastboot into zedboot")
	if _, err := f.Continue(ctx); err != nil {
		return fmt.Errorf("failed to boot the device with \"fastboot continue\": %v", err)
	}
	return nil
}

func (cmd *ZedbootCommand) execute(ctx context.Context, cmdlineArgs []string) error {
	var properties botanist.DeviceProperties
	if err := botanist.LoadDeviceProperties(cmd.propertiesFile, &properties); err != nil {
		return fmt.Errorf("failed to open device properties file \"%v\"", cmd.propertiesFile)
	}

	if err := cmd.validateImages(); err != nil {
		return err
	}

	if properties.PDU != nil {
		defer func() {
			log.Printf("rebooting the node \"%s\"\n", properties.Nodename)

			if err := pdu.RebootDevice(properties.PDU); err != nil {
				log.Fatalf("failed to reboot the device: %v", err)
			}
		}()
	}

	ctx, cancel := context.WithCancel(ctx)

	// Handle SIGTERM and make sure we send a reboot to the device.
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGTERM)

	errs := make(chan error)
	go func() {
		if cmd.fastboot {
			if err := cmd.fastbootToZedboot(ctx); err != nil {
				errs <- err
				return
			}
		}
		errs <- cmd.runTests(ctx, properties.Nodename, cmdlineArgs)
	}()

	select {
	case err := <-errs:
		return err
	case <-signals:
		cancel()
	}

	return nil
}

func (cmd *ZedbootCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	propertiesFlag := f.Lookup("properties")
	log.Printf("properties flag: %v", propertiesFlag.Value)

	if cmd.fastboot {
		if cmd.zedbootImage == "" {
			log.Print("-fastboot is set but -zedboot is not")
			return subcommands.ExitFailure
		}
		if cmd.fastbootTool == "" {
			log.Print("-fastboot is set but -fastboot-tool is empty")
			return subcommands.ExitFailure
		}
	}
	// Aggregate command-line arguments.
	cmdlineArgs := f.Args()
	if cmd.cmdlineFile != "" {
		args, err := ioutil.ReadFile(cmd.cmdlineFile)
		if err != nil {
			log.Printf("failed to read command-line args file \"%v\": %v", cmd.cmdlineFile, err)
			return subcommands.ExitFailure
		}
		cmdlineArgs = append(cmdlineArgs, strings.Split(string(args), "\n")...)
	}

	if err := cmd.execute(ctx, cmdlineArgs); err != nil {
		log.Print(err)
		return subcommands.ExitFailure
	}

	return subcommands.ExitSuccess
}
