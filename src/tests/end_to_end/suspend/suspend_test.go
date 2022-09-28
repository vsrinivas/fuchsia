// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package suspend

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/errutil"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/serial"
)

var c *config

// Err if test takes longer than this time.
const suspendTimeout time.Duration = 5 * time.Minute

// The EC serial path is something in the form of /dev/google/Cr50-X-X.X.X/serial/EC
const baseSerialPath = "/dev/google/"
const serialPathPrefix = "Cr50"
const ecSerialPathSuffix = "/serial/EC"

// These are EC serial commands.
// `powerinfo` returns the device power state
const powerQueryString = "powerinfo"

// `powerbtn` simulates a power button press.
const wakeSignalString = "powerbtn"

// These are the logs we expect to read from EC serial in the test.
const suspendString = "power state 2 = S3"
const resumeString = "power state 7 = S3->S0"

func TestMain(m *testing.M) {
	log.SetPrefix("suspend-test: ")
	log.SetFlags(log.Ldate | log.Ltime | log.LUTC | log.Lshortfile)

	var err error
	c, err = newConfig(flag.CommandLine)
	if err != nil {
		log.Fatalf("failed to create config: %s", err)
	}

	flag.Parse()

	os.Exit(m.Run())
}

func TestSuspend(t *testing.T) {
	ctx := context.Background()
	l := logger.NewLogger(
		logger.TraceLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		"suspend-test: ")
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	if err := doTest(ctx); err != nil {
		logger.Errorf(ctx, "test failed: %v", err)
		errutil.HandleError(ctx, c.deviceConfig.SerialSocketPath, err)
		t.Fatal(err)
	}
}

func doTest(ctx context.Context) error {
	// Connect to target device
	deviceClient, err := c.deviceConfig.NewDeviceClient(ctx)
	if err != nil {
		return fmt.Errorf("failed to create suspend test client: %w", err)
	}
	defer deviceClient.Close()

	l := logger.NewLogger(
		logger.TraceLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		device.NewEstimatedMonotonicTime(deviceClient, "suspend-test: "),
	)
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	return testSuspend(ctx, deviceClient)
}

func testSuspend(
	ctx context.Context,
	device *device.Client,
) error {
	logger.Infof(ctx, "Attempting suspend")

	// Protect against the test stalling out by wrapping it in a closure,
	// setting a timeout on the context, and running the actual test in a
	// closure.
	if err := util.RunWithTimeout(ctx, suspendTimeout, func() error {
		return doTestSuspend(ctx, device)
	}); err != nil {
		return fmt.Errorf("Suspend failed: %w", err)
	}

	return nil
}

func doTestSuspend(
	ctx context.Context,
	device *device.Client,
) error {
	// Get the EC serial device path
	// First try the botanist constant
	ecSerialPath := os.Getenv(constants.ECCableEnvKey)
	// If the botanist constant is not set, we are probably running the test locally.
	// Use filepath discovery to get the serial path.
	if ecSerialPath == "" {
		ecSerialPath = getSerialPath()
		// EC serial is required to resume the device after suspending so if we can't get it,
		// we return an error rather than attempting the test.
		if ecSerialPath == "" {
			return fmt.Errorf("Could not get EC serial path, not attempting suspend")
		}
	}
	logger.Infof(ctx, "EC serial path: %s", ecSerialPath)

	// Try connecting to EC serial. If this fails, abort the test as we will not be able to wake
	// the device from suspend.
	ecAvailable := executeAndCheckLog(ctx, "", "", ecSerialPath)
	if !ecAvailable {
		return fmt.Errorf("Could not connect to EC serial, not attempting suspend")
	}

	if err := device.Suspend(ctx); err != nil {
		return fmt.Errorf("error suspending: %w", err)
	}
	logger.Infof(ctx, "Sent suspend to RAM request 🐐")

	// We sleep here so that the device must reach the suspend state and stay there to pass the
	// test.
	time.Sleep(10 * time.Second)

	// Check the device power state. We expect it to be S3.
	deviceSuspended := executeAndCheckLog(ctx, powerQueryString, suspendString, ecSerialPath)

	if !deviceSuspended {
		return fmt.Errorf("Device is not in the S3 Suspend to RAM power state.")
	}
	logger.Infof(ctx, "Device is in the S3 Suspend to RAM power state 😴💤")

	// Simulate a power button press to wake the device and check that the device attempts to
	// resume.
	deviceResumed := executeAndCheckLog(ctx, wakeSignalString, resumeString, ecSerialPath)
	logger.Infof(ctx, "Sent wake up signal ⏰")

	if !deviceResumed {
		return fmt.Errorf("Device did not attempt to resume.")
	}
	logger.Infof(ctx, "Device attempted resume")

	return nil
}

func getSerialPath() string {
	files, err := ioutil.ReadDir(baseSerialPath)
	if err != nil {
		return ""
	}

	for _, f := range files {
		if strings.HasPrefix(f.Name(), serialPathPrefix) {
			// Add a check that the full path exists
			return fmt.Sprintf("%s%s%s", baseSerialPath, f.Name(), ecSerialPathSuffix)
		}
	}

	return ""
}

func executeAndCheckLog(ctx context.Context, cmd string, checkLog string, serialPath string) bool {
	// Open a ReadWriteCloser to the EC serial line
	localSerialSocket, err := serial.Open(serialPath)
	if err != nil {
		logger.Errorf(ctx, "Could not connect to serial: %s", err)
		return false
	}
	defer localSerialSocket.Close()

	// Send the command to serial
	_, err = io.WriteString(localSerialSocket, fmt.Sprintf("\n%s\n", cmd))
	if err != nil {
		logger.Errorf(ctx, "Could write command to serial: %s", err)
		return false
	}
	logger.Infof(ctx, "Sent '%s' command to serial", cmd)

	if checkLog == "" {
		// There is no log to search for so it is enough that we successfully sent the
		// command.
		return true
	}

	logFound := false

	// We need this sleep to give the logs time to appear
	time.Sleep(100 * time.Millisecond)

	// Look for our expected log string
	scanner := bufio.NewScanner(localSerialSocket)
	for scanner.Scan() {
		logLine := scanner.Text()
		if strings.Contains(logLine, checkLog) {
			logFound = true
			break
		}
	}
	// Only log scanner error if we didn't find our expected log.
	if err = scanner.Err(); err != nil && !logFound {
		logger.Errorf(ctx, "Scanner error: %s", err)
	}

	return logFound
}
