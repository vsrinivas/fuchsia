// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zedmon

import (
	"flag"
	"os"
	"path"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

var fakeZedmonRelpath = flag.String("fake_zedmon_relpath", "",
	"Path to the fake_zedmon binary, relative to the test executable.")

func TestMain(m *testing.M) {
	flag.Parse()
	os.Exit(m.Run())
}

func writeTestData(t *testing.T, basePath string, stderrLines []string, stdoutLines []string) func() {
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

func setupTest(t *testing.T, stderrData, stdinData []string) (string, func()) {
	myDir, err := filepath.Abs(filepath.Dir(os.Args[0]))
	if err != nil {
		t.Fatalf(err.Error())
	}
	fakeZedmonPath := path.Join(myDir, *fakeZedmonRelpath)
	if _, err := os.Stat(fakeZedmonPath); os.IsNotExist(err) {
		t.Fatalf("fake_zedmon executable not found at %s", fakeZedmonPath)
	}

	cleanup := writeTestData(t, fakeZedmonPath, stderrData, stdinData)
	return fakeZedmonPath, cleanup
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

func expectErrorWithSubstring(t *testing.T, dataChannel chan []byte, errorChannel chan error, substring string) {
	select {
	case err := <-errorChannel:
		if !strings.Contains(err.Error(), substring) {
			t.Fatalf("Received wrong error (%v). Expected substring %s", err, substring)
		}
		break
	case <-dataChannel:
		t.Fatal("Shouldn't have received data")
		return
	}
}

func TestSuccessfulZedmonInput(t *testing.T) {
	fakeZedmonPath, cleanup := setupTest(
		t,
		[]string{
			"2019/09/24 12:38:24 Time offset: 1569350976091349587ns ± 228725ns",
			"2019/09/24 12:38:24 Starting report recording. Send ^C to stop.",
		},
		[]string{
			"2928214933,0.0016949999571806984,13.741249692859128,2.329141816160927",
			"2928217533,0.0015974999596437556,13.721249693306163,2.191969632126188",
			"2928218183,0.0015349999612226384,13.717499693389982,2.105636196807154",
			"2928218833,0.0019349999511177884,13.67249969439581,2.6456286831657962",
			"2928219483,0.0021124999466337613,13.696249693864956,2.8933327394082653",
			"2928220134,0.0020224999489073525,13.726249693194404,2.7761339923689548",
			"2928220784,0.0018799999525072053,13.727499693166465,2.580769934804266",
			"2928221434,0.002049999948212644,13.727499693166465,2.814137428908907",
			"2928222084,0.0022724999425918213,13.702499693725258,3.113893046336443",
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
	fakeZedmonPath, cleanup := setupTest(
		t,
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
	expectErrorWithSubstring(t, dataChannel, errorChannel, "record length")
}

func TestBadTimestamp(t *testing.T) {
	fakeZedmonPath, cleanup := setupTest(
		t,
		[]string{
			"2019/09/24 12:38:24 Time offset: 1569350976091349587ns ± 228725ns",
			"2019/09/24 12:38:24 Starting report recording. Send ^C to stop.",
		},
		[]string{
			"ABCDEF,0.0015349999612226384,13.717499693389982,2.105636196807154",
		})
	defer cleanup()

	dataChannel, errorChannel := runZedmon(t, fakeZedmonPath, true)
	expectErrorWithSubstring(t, dataChannel, errorChannel, "timestamp")
}

func TestBadShuntVoltageRecord(t *testing.T) {
	fakeZedmonPath, cleanup := setupTest(
		t,
		[]string{
			"2019/09/24 12:38:24 Time offset: 1569350976091349587ns ± 228725ns",
			"2019/09/24 12:38:24 Starting report recording. Send ^C to stop.",
		},
		[]string{
			"2928218183,ABCDEF,13.717499693389982,2.105636196807154",
		})
	defer cleanup()

	dataChannel, errorChannel := runZedmon(t, fakeZedmonPath, true)
	expectErrorWithSubstring(t, dataChannel, errorChannel, "shunt voltage")
}

func TestBadBusVoltageRecord(t *testing.T) {
	fakeZedmonPath, cleanup := setupTest(
		t,
		[]string{
			"2019/09/24 12:38:24 Time offset: 1569350976091349587ns ± 228725ns",
			"2019/09/24 12:38:24 Starting report recording. Send ^C to stop.",
		},
		[]string{
			"2928218183,0.0015349999612226384,ABCDEF,2.105636196807154",
		})
	defer cleanup()

	dataChannel, errorChannel := runZedmon(t, fakeZedmonPath, true)
	expectErrorWithSubstring(t, dataChannel, errorChannel, "bus voltage")
}

func TestBadPowerRecord(t *testing.T) {
	fakeZedmonPath, cleanup := setupTest(
		t,
		[]string{
			"2019/09/24 12:38:24 Time offset: 1569350976091349587ns ± 228725ns",
			"2019/09/24 12:38:24 Starting report recording. Send ^C to stop.",
		},
		[]string{
			"2928218183,0.0015349999612226384,13.717499693389982,ABCDEF",
		})
	defer cleanup()

	dataChannel, errorChannel := runZedmon(t, fakeZedmonPath, true)
	expectErrorWithSubstring(t, dataChannel, errorChannel, "power")
}
