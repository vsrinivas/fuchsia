// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path"
	"time"

	"github.com/google/subcommands"
	"go.fuchsia.dev/fuchsia/garnet/bin/traceutil/zedmon"
)

type cmdRecord struct {
	flags *flag.FlagSet

	targetHostname string
	targetPort     string
	keyFile        string
	filePrefix     string
	reportType     string
	stdout         bool
	zedmon         string
	captureConfig  *captureTraceConfig
}

type reportGenerator struct {
	generatorPath    string
	outputFileSuffix string
}

var (
	builtinReports = map[string]reportGenerator{
		"html": {getHtmlGenerator(), "html"},
	}
)

func NewCmdRecord() *cmdRecord {
	cmd := &cmdRecord{
		flags: flag.NewFlagSet("record", flag.ExitOnError),
	}
	cmd.flags.StringVar(&cmd.keyFile, "key-file", "", "SSH key file to use. The default is ~/.ssh/fuchsia_ed25519.")
	cmd.flags.StringVar(&cmd.filePrefix, "file-prefix", "",
		"Prefix for trace file names.  Defaults to 'trace-<timestamp>'.")
	cmd.flags.StringVar(&cmd.targetHostname, "target", "", "Target hostname. Can also be set in environment with TRACEUTIL_TARGET_HOST.")
	cmd.flags.StringVar(&cmd.targetPort, "target-port", "", "Target SSH port. Can also be set in environment with TRACEUTIL_TARGET_PORT.")
	cmd.flags.StringVar(&cmd.reportType, "report-type", "html", "Report type.")
	cmd.flags.BoolVar(&cmd.stdout, "stdout", false,
		"Send the report to stdout, in addition to writing to file.")
	cmd.flags.StringVar(&cmd.zedmon, "zedmon", "",
		"UNDER DEVELOPMENT: Path to power trace utility, zedmon.")

	target, present := os.LookupEnv("TRACEUTIL_TARGET_HOST")
	if present {
		cmd.flags.Set("target", target)
	}

	port, present := os.LookupEnv("TRACEUTIL_TARGET_PORT")
	if present {
		cmd.flags.Set("target-port", port)
	}

	cmd.captureConfig = newCaptureTraceConfig(cmd.flags)
	return cmd
}

func (*cmdRecord) Name() string {
	return "record"
}

func (*cmdRecord) Synopsis() string {
	return "Record a trace on a target, download, and convert to HTML."
}

func (cmd *cmdRecord) Usage() string {
	usage := "traceutil record <options>\n"
	cmd.flags.Visit(func(flag *flag.Flag) {
		usage += flag.Usage
	})

	return usage
}

func (cmd *cmdRecord) SetFlags(f *flag.FlagSet) {
	*f = *cmd.flags
}

func (cmd *cmdRecord) Execute(_ context.Context, f *flag.FlagSet,
	_ ...interface{}) subcommands.ExitStatus {
	checkBuildConfiguration()

	// Flag errors in report type early.
	reportGenerator := builtinReports[cmd.reportType]
	generatorPath := ""
	outputFileSuffix := ""
	if reportGenerator.generatorPath != "" {
		generatorPath = reportGenerator.generatorPath
		outputFileSuffix = reportGenerator.outputFileSuffix
	} else {
		generatorPath = getExternalReportGenerator(cmd.reportType)
		outputFileSuffix = cmd.reportType
	}
	fmt.Printf("generator path: %s\n", generatorPath)
	if _, err := os.Stat(generatorPath); os.IsNotExist(err) {
		fmt.Printf("No generator for report type \"%s\"\n",
			cmd.reportType)
		return subcommands.ExitFailure
	}

	// Establish connection to runtime host.
	conn, err := NewTargetConnection(cmd.targetHostname, cmd.targetPort, cmd.keyFile)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}
	defer conn.Close()

	// Zedmon: Sync clock with runtime host to enable eventual
	// zedmon -> devhost -> runtime host clock domain transformation.
	var fOffset, fDelta time.Duration
	doZedmon := cmd.zedmon != ""
	if doZedmon {
		fOffset, fDelta, err = conn.SyncClk()
		if err != nil {
			fmt.Printf("Error syncing with device clock: %v\n", err)
			doZedmon = false
		} else {
			fmt.Printf("Synced fuchsia clock: Offset: %v, ±%v\n", fOffset, fDelta)
		}
	}

	// Establish local and remote files for managing trace data.
	prefix := cmd.getFilenamePrefix()
	var localFilename string
	if cmd.captureConfig.Binary {
		localFilename = prefix + ".fxt"
	} else {
		localFilename = prefix + ".json"
	}
	outputFilename := prefix + "." + outputFileSuffix
	// TODO(dje): Should we use prefix on the remote file name as well?
	var remoteFilename string
	if cmd.captureConfig.Binary {
		remoteFilename = "/tmp/trace.fxt"
	} else {
		remoteFilename = "/tmp/trace.json"
	}
	var localFile *os.File = nil
	if cmd.captureConfig.Stream {
		localFile, err = os.Create(localFilename)
		if err != nil {
			fmt.Printf("Error creating intermediate file %s: %s\n",
				localFilename, err.Error())
			return subcommands.ExitFailure
		}
	} else if cmd.captureConfig.Compress {
		localFilename += ".gz"
		remoteFilename += ".gz"
	}

	if f.NArg() > 0 {
		cmd.captureConfig.Command = f.Args()
	}

	// Zedmon: Start capturing data from zedmon device.
	var z *zedmon.Zedmon
	var zDataChan chan []byte
	var zErrChan chan error
	if doZedmon {
		z = &zedmon.Zedmon{}
		zDataChan, zErrChan, _ = z.Run(fOffset, fDelta, cmd.zedmon)
	}

	// Capture trace data from runtime host.
	err = captureTrace(cmd.captureConfig, conn, localFile)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}

	// Zedmon: Stop capturing data, emit errors that occurred in the meantime.
	var zData []byte
	zErrs := make([]error, 0)
	if doZedmon {
		err = z.Stop()
		if err != nil {
			// No `doZedmon = false`; attempt to read whatever samples we can.
			fmt.Printf("Error stopping zedmon: %v\n", err)
		} else {
			fmt.Printf("Zedmon data collection stopped cleanly\n")
		}

		timeout := make(chan bool, 1)
		go func() {
			time.Sleep(2 * time.Second)
			timeout <- true
		}()

		func() {
			for {
				select {
				case err := <-zErrChan:
					fmt.Printf("Warning: Zedmon error: %v\n", err)
					zErrs = append(zErrs, err)
				case zData = <-zDataChan:
					fmt.Printf("Collected %d-byte trace from zedmon\n", len(zData))
					return
				case <-timeout:
					fmt.Printf("Failed to collect zedmon data: Channel read timeout\n")
					return
				}
			}
		}()

		doZedmon = zData != nil
	}

	// Download file in non-streaming case.
	if !cmd.captureConfig.Stream {
		err = cmd.downloadFile(conn, "trace", remoteFilename, localFilename)
		if err != nil {
			fmt.Println(err.Error())
			return subcommands.ExitFailure
		}
	}

	// Benchmark results: Download corresponding file.
	if len(cmd.captureConfig.BenchmarkResultsFile) > 0 {
		err = cmd.downloadFile(conn, cmd.captureConfig.BenchmarkResultsFile,
			cmd.captureConfig.BenchmarkResultsFile,
			cmd.captureConfig.BenchmarkResultsFile)
		if err != nil {
			fmt.Println(err)
			return subcommands.ExitFailure
		}
		fmt.Println("done")
	}

	// TODO(TO-403): Remove remote file.  Add command line option to leave it.

	title := cmd.getReportTitle()

	jsonFilename := localFilename
	if cmd.captureConfig.Binary {
		jsonFilename = replaceFilenameExt(localFilename, "json")
		jsonGenerator := getJsonGenerator()
		err = convertToJson(jsonGenerator, cmd.captureConfig.Compress, jsonFilename, localFilename)
		if err != nil {
			fmt.Println(err.Error())
			return subcommands.ExitFailure
		}
	}

	// Zedmon: Include additional zedmon trace data file in HTML output.
	if doZedmon {
		zFilename := "zedmon-" + prefix + ".json"
		err = ioutil.WriteFile(zFilename, zData, 0644)
		if err != nil {
			fmt.Printf("Failed to write zedmon trace to file")
			err = convertToHtml(generatorPath, outputFilename, title, jsonFilename)
		} else {
			err = convertToHtml(generatorPath, outputFilename, title, jsonFilename, zFilename)
		}
	} else {
		err = convertToHtml(generatorPath, outputFilename, title, jsonFilename)
	}
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}

	// Handle special report-to-stdout case.
	if cmd.stdout {
		report, openErr := os.Open(outputFilename)
		if openErr != nil {
			fmt.Println(openErr.Error())
			return subcommands.ExitFailure
		}
		_, reportErr := io.Copy(os.Stdout, report)
		if reportErr != nil {
			fmt.Println(reportErr.Error())
			return subcommands.ExitFailure
		}
	}

	return subcommands.ExitSuccess
}

func (cmd *cmdRecord) downloadFile(conn *TargetConnection, name string, remotePath string, localPath string) error {
	now := time.Now()

	fmt.Printf("Downloading %s ... ", name)
	err := conn.GetFile(remotePath, localPath)
	if err != nil {
		fmt.Println(err.Error())
		return err
	}
	fmt.Println("done")

	elapsed := time.Since(now)
	fileInfo, err2 := os.Stat(localPath)
	fmt.Printf("Downloaded %s in %0.3f seconds",
		path.Base(localPath), elapsed.Seconds())
	if err2 == nil {
		fmt.Printf(" (%0.3f KB/sec)",
			float64((fileInfo.Size()+1023)/1024)/elapsed.Seconds())
	} else {
		fmt.Printf(" (unable to determine download speed: %s)", err2.Error())
	}
	fmt.Printf("\n")

	return nil
}

func (cmd *cmdRecord) getFilenamePrefix() string {
	if cmd.filePrefix == "" {
		// Use ISO_8601 date time format.
		return "trace-" + time.Now().Format("2006-01-02T15:04:05")
	} else {
		return cmd.filePrefix
	}
}

func (cmd *cmdRecord) getReportTitle() string {
	conf := cmd.captureConfig
	have_categories := conf.Categories != ""
	have_duration := conf.Duration != 0
	text := ""
	if have_categories {
		text = text + ", categories " + conf.Categories
	}
	if have_duration {
		text = text + ", duration " + conf.Duration.String()
	}
	text = text[2:]
	if text == "" {
		return ""
	}
	return fmt.Sprintf("Report for %s", text)
}
