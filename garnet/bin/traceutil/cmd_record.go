package main

import (
	"bufio"
	"context"
	"encoding/csv"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
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
	stdout io.ReadCloser
	csvout *csv.Reader
	stderr io.ReadCloser
	err    error
}

type zTraceReport struct {
	DisplayTimeUnit string        `json:"displayTimeUnit"`
	TraceEvents     []interface{} `json:"traceEvents"`
	//SystemTraceEvents zSystemTraceEvents `json:"systemTraceEvents"`
}

type zSystemTraceEvents struct {
	Type   string        `json:"type"`
	Events []interface{} `json:"events"`
}

type zMetadataEvent struct {
	Type string        `json:"ph"`
	PID  int           `json:"pid"`
	Name string        `json:"name"`
	Args zMetadataArgs `json:"args"`
}

type zMetadataArgs struct {
	Name string `json:"name"`
}

type zCompleteDurationEvent struct {
	Type      string  `json:"ph"`
	PID       int     `json:"pid"`
	Name      string  `json:"name"`
	Timestamp float64 `json:"ts"`
	Duration  float64 `json:"dur"`
}

type zCounterEvent struct {
	Type      string      `json:"ph"`
	PID       int         `json:"pid"`
	Name      string      `json:"name"`
	Timestamp float64     `json:"ts"`
	Values    zTraceValue `json:"args"`
}

type zTraceValue struct {
	Voltage float32 `json:"voltage"`
}

const zedmonPID = 2053461101 // "zedm" = 0x7a65546d = 2053461101.

func newZTraceReport(events []interface{}) zTraceReport {
	return zTraceReport{
		DisplayTimeUnit: "ns",
		TraceEvents:     events,
		// SystemTraceEvents: zSystemTraceEvents{
		// 	Type:   "fuchsia",
		// 	Events: events,
		// },
	}
}

func newZMetadataEvent() zMetadataEvent {
	return zMetadataEvent{
		Type: "M",
		PID:  zedmonPID,
		Name: "process_name",
		Args: zMetadataArgs{
			Name: "zedmon",
		},
	}
}

func newZCompleteDurationEvent(name string, ts time.Time, dur time.Duration) zCompleteDurationEvent {
	tus := float64(ts.UnixNano()) / 1000
	dus := float64(dur.Nanoseconds()) / 1000
	return zCompleteDurationEvent{
		Type:      "X",
		PID:       zedmonPID,
		Name:      name,
		Timestamp: tus,
		Duration:  dus,
	}
}

func newZCounterEvents(ts time.Time, delta time.Duration, vShunt, vBus float32) []interface{} {
	errStart := ts.Add(-delta / 2)
	us := float64(ts.UnixNano()) / 1000
	return []interface{}{
		newZCompleteDurationEvent(fmt.Sprintf("shunt:%f;bus:%f", vShunt, vBus), errStart, delta),
		zCounterEvent{
			Type:      "C",
			PID:       zedmonPID,
			Name:      "Shunt voltage",
			Timestamp: us,
			Values: zTraceValue{
				Voltage: vShunt,
			},
		},
		zCounterEvent{
			Type:      "C",
			PID:       zedmonPID,
			Name:      "Bus voltage",
			Timestamp: us,
			Values: zTraceValue{
				Voltage: vBus,
			},
		},
	}
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

	// Establish connection to runtime host.
	conn, err := NewTargetConnection(cmd.targetHostname, cmd.keyFile)
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

	// Zedmon: Start capturing data from zedmon device.
	var z *zedmon
	var zDataChan chan []byte
	var zErrChan chan error
	if doZedmon {
		z = newZedmon()
		zDataChan, zErrChan = z.run(fOffset, fDelta, cmd.zedmon)
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
		err = z.stop()
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

	// TODO(PT-113): Convert binary format to JSON and HTML when able.
	if !cmd.captureConfig.Binary {
		// Zedmon: Include additional zedmon trace data file in HTML output.
		if doZedmon {
			zFilename := "zedmon-" + prefix + ".json"
			err = ioutil.WriteFile(zFilename, zData, 0644)
			if err != nil {
				fmt.Printf("Failed to write zedmon trace to file")
				err = convertTrace(generatorPath, outputFilename, title, localFilename)
			} else {
				err = convertTrace(generatorPath, outputFilename, title, localFilename, zFilename)
			}
		} else {
			err = convertTrace(generatorPath, outputFilename, title, localFilename)
		}
		if err != nil {
			fmt.Println(err.Error())
			return subcommands.ExitFailure
		}
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

func newZedmon() *zedmon {
	return &zedmon{}
}

var zRegExp = regexp.MustCompile("Time offset: ([0-9]+)ns ± ([0-9]+)ns$")

func (z *zedmon) fail(err error) error {
	if z == nil {
		return err
	}
	if z.stdout != nil {
		z.stdout.Close()
	}
	if z.stderr != nil {
		z.stderr.Close()
	}
	if z.cmd != nil && z.cmd.Process != nil {
		z.cmd.Process.Kill()
	}
	z.cmd = nil
	z.stdout = nil
	z.csvout = nil
	z.stderr = nil
	return err
}

func (z *zedmon) run(fOffset, fDelta time.Duration, path string) (data chan []byte, errs chan error) {
	data = make(chan []byte)
	errs = make(chan error)
	go z.doRun(fOffset, fDelta, path, data, errs)
	return data, errs
}

func (z *zedmon) doRun(fOffset, fDelta time.Duration, path string, data chan []byte, errs chan error) {
	// TODO(markdittmer): Add error delta to trace.
	zOffset, zDelta, err := z.start(path)
	if err != nil {
		errs <- err
		return
	}
	fmt.Printf("Synced zedmon clock: Offset: %v, ±%v\n", zOffset, zDelta)

	offset := zOffset - fOffset
	delta := 2 * (fDelta + zDelta)

	events := make([]interface{}, 2)
	events[0] = newZMetadataEvent()
	var t0 time.Time
	for {
		strs, err := z.csvout.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			errs <- z.fail(errors.New("Failed to parse CSV record"))
			break
		}
		if len(strs) != 3 {
			errs <- z.fail(errors.New("Unexpected CSV record length"))
			break
		}
		ts, err := strconv.ParseInt(strs[0], 10, 64)
		if err != nil {
			errs <- z.fail(errors.New("Failed to parse timestamp from CSV"))
			break
		}
		vShunt, err := strconv.ParseFloat(strs[1], 64)
		if err != nil {
			errs <- z.fail(errors.New("Failed to parse voltage from CSV"))
			break
		}
		vBus, err := strconv.ParseFloat(strs[2], 64)
		if err != nil {
			errs <- z.fail(errors.New("Failed to parse voltage from CSV"))
			break
		}

		t := time.Unix(int64(ts/1000000), int64((ts%1000000)*1000)).Add(offset)
		if t0 == (time.Time{}) {
			t0 = t
		}
		events = append(events, newZCounterEvents(t, delta, float32(vShunt), float32(vBus))...)
	}
	events[1] = newZCompleteDurationEvent("maxTimeSyncErr", t0, delta)

	// Drop last event: may be partial line from CSV stream.
	if len(events) > 2 {
		events = events[:len(events)-1]
	}

	d, err := json.Marshal(newZTraceReport(events))
	if err != nil {
		errs <- err
		return
	}

	data <- d
}

func (z *zedmon) start(path string) (offset time.Duration, delta time.Duration, err error) {
	if z == nil {
		return offset, delta, z.fail(errors.New("Nil zedmon"))
	}
	if z.cmd != nil || z.stderr != nil || z.stdout != nil || z.csvout != nil || z.err != nil {
		return offset, delta, z.fail(errors.New("Attempt to reuse zedmon object"))
	}

	z.cmd = exec.Command(path, "record", "-out", "-")
	z.cmd.Dir, err = os.Getwd()
	if err != nil {
		return offset, delta, z.fail(errors.New("Failed to get working directory"))
	}
	z.stdout, err = z.cmd.StdoutPipe()
	if err != nil {
		return offset, delta, z.fail(err)
	}
	z.csvout = csv.NewReader(z.stdout)
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
		matches := zRegExp.FindSubmatch(l[:len(l)-1])
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
		break
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
