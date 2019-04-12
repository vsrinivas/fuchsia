package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path"
	"regexp"
	"strconv"
	"syscall"
	"time"

	"github.com/google/subcommands"
)

type cmdRecord struct {
	flags *flag.FlagSet

	targetHostname string
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

type zedmon struct {
	cmd    *exec.Cmd
	stderr io.ReadCloser
	err    error
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
	cmd.flags.StringVar(&cmd.keyFile, "key-file", "", "SSH key file to use. The default is $FUCHSIA_DIR/.ssh/pkey.")
	cmd.flags.StringVar(&cmd.filePrefix, "file-prefix", "",
		"Prefix for trace file names.  Defaults to 'trace-<timestamp>'.")
	cmd.flags.StringVar(&cmd.targetHostname, "target", "", "Target hostname.")
	cmd.flags.StringVar(&cmd.reportType, "report-type", "html", "Report type.")
	cmd.flags.BoolVar(&cmd.stdout, "stdout", false,
		"Send the report to stdout, in addition to writing to file.")
	cmd.flags.StringVar(&cmd.zedmon, "zedmon", "",
		"UNDER DEVELOPMENT: Path to power trace utility, zedmon.")

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

	conn, err := NewTargetConnection(cmd.targetHostname, cmd.keyFile)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}
	defer conn.Close()

	var fOffset, fDelta time.Duration
	doZedmon := cmd.zedmon != ""
	if doZedmon {
		fOffset, fDelta, err = conn.SyncClk()
		if err != nil {
			fmt.Printf("Error syncing with device clock: %v\n", err)
			doZedmon = false
		} else {
			fmt.Printf("Synced device clock: Offset: %v, ±%v\n", fOffset, fDelta)
		}
	}

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
		remoteFilename = "/data/trace.fxt"
	} else {
		remoteFilename = "/data/trace.json"
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

	var z *zedmon
	var zOffset, zDelta time.Duration
	if doZedmon {
		z = newZedmon()
		zOffset, zDelta, err = z.start(cmd.zedmon)
		if err != nil {
			fmt.Printf("Error syncing with zedmon clock: %v\n", err)
			doZedmon = false
		} else {
			fmt.Printf("Synced zedmon clock: Offset: %v, ±%v\n", zOffset, zDelta)
		}
	}

	err = captureTrace(cmd.captureConfig, conn, localFile)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}

	if doZedmon {
		err = z.stop()
		if err != nil {
			// No `doZedmon = false`; attempt to read whatever samples we can.
			fmt.Printf("Error stopping zedmon: %v\n", err)
		} else {
			fmt.Printf("Zedmon data collection stopped cleanly\n")
		}
	}

	if !cmd.captureConfig.Stream {
		err = cmd.downloadFile(conn, "trace", remoteFilename, localFilename)
		if err != nil {
			fmt.Println(err.Error())
			return subcommands.ExitFailure
		}
	}

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

	if doZedmon {
		// TODO(markdittmer): Read from zedmon data and convert to trace input.
	}

	// TODO(TO-403): Remove remote file.  Add command line option to leave it.

	title := cmd.getReportTitle()

	// TODO(PT-113): Convert binary format to JSON and HTML when able.
	if !cmd.captureConfig.Binary {
		err = convertTrace(generatorPath, localFilename, outputFilename, title)
		if err != nil {
			fmt.Println(err.Error())
			return subcommands.ExitFailure
		}
	}

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

func newZedmon() *zedmon {
	return &zedmon{}
}

var zRegExp = regexp.MustCompile("^Time offset: ([0-9]+)ns ± ([0-9]+)ns$")

func (z *zedmon) fail(err error) error {
	if z == nil {
		return err
	}
	z.cmd = nil
	z.stderr = nil
	if z.stderr != nil {
		z.stderr.Close()
	}
	if z.cmd != nil && z.cmd.Process != nil {
		z.cmd.Process.Kill()
	}
	return err
}

func (z *zedmon) start(path string) (offset time.Duration, delta time.Duration, err error) {
	if z == nil {
		return offset, delta, z.fail(errors.New("Nil zedmon"))
	}
	if z.cmd != nil || z.stderr != nil || z.err != nil {
		return offset, delta, z.fail(errors.New("Attempt to reuse zedmon object"))
	}

	z.cmd = exec.Command(path, "record")
	z.cmd.Dir, err = os.Getwd()
	if err != nil {
		return offset, delta, z.fail(errors.New("Failed to get working directory"))
	}
	z.stderr, err = z.cmd.StderrPipe()
	if err != nil {
		return offset, delta, z.fail(err)
	}
	r := bufio.NewReader(z.stderr)

	if err = z.cmd.Start(); err != nil {
		return offset, delta, z.fail(err)
	}

	nl := byte('\n')
	for l, err := r.ReadBytes(nl); err == nil; l, err = r.ReadBytes(nl) {
		matches := zRegExp.FindSubmatch(l)
		if len(matches) != 3 {
			continue
		}

		o, err := strconv.ParseInt(string(matches[1]), 10, 64)
		if err != nil {
			return offset, delta, z.fail(errors.New("Failed to parse time sync offset"))
		}
		offset = time.Nanosecond * time.Duration(o)
		d, err := strconv.ParseInt(string(matches[2]), 10, 64)
		if err != nil {
			z.cmd.Process.Kill()
			return offset, delta, z.fail(errors.New("Failed to parse time sync delta"))
		}
		delta = time.Nanosecond * time.Duration(d)
	}

	if err != nil {
		return offset, delta, z.fail(err)
	}

	return offset, delta, err
}

func (z *zedmon) stop() error {
	if z == nil {
		return z.fail(errors.New("Nil zedmon"))
	}
	if z.cmd == nil || z.cmd.Process == nil {
		return z.fail(errors.New("No zedmon command/process"))
	}
	err := z.cmd.Process.Signal(syscall.SIGINT)
	if err != nil {
		return z.fail(errors.New("Failed to send zedmon process SIGINT"))
	}
	err = z.cmd.Wait()
	z.cmd = nil
	z.stderr = nil
	return err
}
