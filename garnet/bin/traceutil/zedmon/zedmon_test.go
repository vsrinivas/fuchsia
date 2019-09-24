// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zedmon

import (
	"os"
	"path"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func WriteTestData(t *testing.T, basePath string, stderrLines []string, stdoutLines []string) func() {
	stderrDataPath := basePath + ".stderr.testdata"
	stdoutDataPath := basePath + ".stdout.testdata"

	stderrFile, err := os.Create(stderrDataPath)
	if err != nil {
		t.Fatalf("Failed to create file %s, error %v", stderrDataPath, err)
	}
	for _, line := range stderrLines {
		stderrFile.WriteString(line)
		stderrFile.WriteString("\n")
	}
	stderrFile.Close()

	stdoutFile, err := os.Create(stdoutDataPath)
	if err != nil {
		t.Fatalf("Failed to create file %s, error %v", stdoutDataPath, err)
	}
	for _, line := range stdoutLines {
		stdoutFile.WriteString(line)
		stdoutFile.WriteString("\n")
	}
	stdoutFile.Close()

	return func() {
		os.Remove(stderrDataPath)
		os.Remove(stdoutDataPath)
	}
}

func runZedmon(t *testing.T, fakeZedmonPath string, expectError bool) (chan []byte, chan error) {
	z := Zedmon{}
	dataChannel, errorChannel, startedChannel := z.Run(time.Duration(0), time.Duration(0), fakeZedmonPath)

	// NOTE: When debugging tests, it is useful to run the following goroutine:
	//
	// go func() {
	// 	e := <-errorChannel
	// 	fmt.Printf("Error: %v\n", e)
	// }()
	//
	// This will prevent errors from reaching tests that expect them, but it will display errors
	// that will otherwise manifest as an opaque failure in z.Stop().

	// We can't safely run z.Stop() until the Zedmon signals that it has started.
	<-startedChannel

	if !expectError {
		// Stop the Zedmon. fake_zedmon will trap the SIGINT it receives and finish writing its
		// data.
		err := z.Stop()
		if err != nil {
			t.Fatalf(err.Error())
		}
	}

	return dataChannel, errorChannel
}

func TestSuccessfulZedmonInput(t *testing.T) {
	myDir, err := filepath.Abs(filepath.Dir(os.Args[0]))
	if err != nil {
		t.Fatalf(err.Error())
	}

	fakeZedmonPath := path.Join(myDir, "fake_zedmon")
	cleanup := WriteTestData(
		t, fakeZedmonPath,
		[]string{
			"2019/09/16 14:50:19 Time offset: 1568667086301265685ns ± 185703ns",
			"2019/09/16 14:50:19 Starting report recording. Send ^C to stop.",
		},
		[]string{
			"3533624760,0.0013324999663382187,13.716249693417922",
			"3533625410,0.0012999999671592377,13.716249693417922",
			"3533626710,0.0013199999666539952,13.718749693362042",
			"3533629310,0.0013174999667171505,13.717499693389982",
			"3533629959,0.0013124999668434612,13.718749693362042",
			"3533630623,0.0012924999673487036,13.717499693389982",
			"3533631273,0.0012874999674750143,13.717499693389982",
			"3533631923,0.0012549999682960333,13.718749693362042",
			"3533632573,0.0012799999676644802,13.722499693278223",
		})
	defer cleanup()

	dataChannel, errorChannel := runZedmon(t, fakeZedmonPath, false)

	var data []byte
	func() {
		for {
			select {
			case err := <-errorChannel:
				t.Fatalf("Zedmon error: %v", err)
			case data = <-dataChannel:
				return
			}
		}
	}()

	if len(data) == 0 {
		t.Fatal("Zero bytes written")
	}

}

func TestWrongRecordLength(t *testing.T) {
	myDir, err := filepath.Abs(filepath.Dir(os.Args[0]))
	if err != nil {
		t.Fatalf(err.Error())
	}

	fakeZedmonPath := path.Join(myDir, "fake_zedmon")
	cleanup := WriteTestData(
		t, fakeZedmonPath,
		[]string{
			"2019/09/16 14:50:19 Time offset: 1568667086301265685ns ± 185703ns",
			"2019/09/16 14:50:19 Starting report recording. Send ^C to stop.",
		},
		[]string{
			"3533624760,0.0013324999663382187",
			"3533625410,0.0012999999671592377",
			"3533626710,0.0013199999666539952",
		})
	defer cleanup()

	dataChannel, errorChannel := runZedmon(t, fakeZedmonPath, true)

	// We expect an error due to wrong record length.
	select {
	case err = <-errorChannel:
		if !strings.Contains(err.Error(), "record length") {
			t.Fatalf("Received wrong error: %v", err)
		}
		break
	case <-dataChannel:
		t.Fatal("Shouldn't have received data")
		return
	}
}
