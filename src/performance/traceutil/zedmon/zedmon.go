// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zedmon

import (
	"bufio"
	"encoding/csv"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"sync"
	"syscall"
	"time"
)

type Zedmon struct {
	cmd    *exec.Cmd
	stdout io.ReadCloser
	csvout *csv.Reader
	stderr io.ReadCloser
	err    error

	// Synchronizes use of `stdout` between `csvout.Read`, which reads from it, and `cmd.Wait`,
	// which closes it. Also guards the clearing of `cmd`, which signals that reads from stdout
	// should stop.
	stdoutMutex sync.Mutex
}

type zTraceReport struct {
	DisplayTimeUnit string        `json:"displayTimeUnit"`
	TraceEvents     []interface{} `json:"traceEvents"`
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
	Value float32 `json:"value"`
}

const zedmonPID = 2053461101 // "zedm" = 0x7a65546d = 2053461101.

func newZTraceReport(events []interface{}) zTraceReport {
	return zTraceReport{
		DisplayTimeUnit: "ns",
		TraceEvents:     events,
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

func newZCounterEvents(ts time.Time, delta time.Duration, vShunt, vBus, power float32) []interface{} {
	errStart := ts.Add(-delta / 2)
	us := float64(ts.UnixNano()) / 1000
	return []interface{}{
		newZCompleteDurationEvent(fmt.Sprintf("v_shunt:%f;v_bus:%f,power:%f", vShunt, vBus, power), errStart, delta),
		zCounterEvent{
			Type:      "C",
			PID:       zedmonPID,
			Name:      "Shunt voltage",
			Timestamp: us,
			Values: zTraceValue{
				Value: vShunt,
			},
		},
		zCounterEvent{
			Type:      "C",
			PID:       zedmonPID,
			Name:      "Bus voltage",
			Timestamp: us,
			Values: zTraceValue{
				Value: vBus,
			},
		},
		zCounterEvent{
			Type:      "C",
			PID:       zedmonPID,
			Name:      "Power",
			Timestamp: us,
			Values: zTraceValue{
				Value: power,
			},
		},
	}
}

var zRegExp = regexp.MustCompile("Time offset: ([0-9]+)ns ± ([0-9]+)ns$")

func (z *Zedmon) fail(err error) error {
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

func (z *Zedmon) Run(fOffset, fDelta time.Duration, path string) (data chan []byte, errs chan error, started chan bool) {
	data = make(chan []byte)
	errs = make(chan error)
	started = make(chan bool, 1)
	go z.doRun(fOffset, fDelta, path, data, errs, started)
	return data, errs, started
}

func (z *Zedmon) doRun(fOffset, fDelta time.Duration, path string, data chan []byte, errs chan error, started chan bool) {
	// Guards the `started` channel. See comments below, where channel is notified.
	startSignalled := false

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
		z.stdoutMutex.Lock()
		// If the underlying Zedmon client process has been terminated, stop reading.
		if z.cmd == nil {
			z.stdoutMutex.Unlock()
			break
		}

		strs, err := z.csvout.Read()
		z.stdoutMutex.Unlock()

		// The `started` channel is a hack to synchronize with unit tests. A test needs to be able
		// to guarantee that the underlying (probably fake) zedmon process has registered its SIGINT
		// handler, and that `z.csvout` has processed some data.
		//
		// It would be better if `Zedmon` emitted records as it processed them. However, it would
		// likely be better to invest the effort in converting traceutil to Dart (see fxbug.dev/48045),
		// in which there is already a Zedmon interface that behaves accordingly.
		if !startSignalled {
			started <- true
			startSignalled = true
		}

		if err == io.EOF {
			break
		}
		if err != nil {
			errs <- z.fail(fmt.Errorf("Failed to parse CSV record; reader error: %s", err))
			break
		}
		if len(strs) != 4 {
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
			errs <- z.fail(errors.New("Failed to parse shunt voltage from CSV"))
			break
		}
		vBus, err := strconv.ParseFloat(strs[2], 64)
		if err != nil {
			errs <- z.fail(errors.New("Failed to parse bus voltage from CSV"))
			break
		}
		power, err := strconv.ParseFloat(strs[3], 64)
		if err != nil {
			errs <- z.fail(errors.New("Failed to parse power from CSV"))
			break
		}
		t := time.Unix(int64(ts/1000000), int64((ts%1000000)*1000)).Add(offset)
		if t0 == (time.Time{}) {
			t0 = t
		}
		events = append(events, newZCounterEvents(t, delta, float32(vShunt), float32(vBus), float32(power))...)
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

func (z *Zedmon) start(path string) (offset time.Duration, delta time.Duration, err error) {
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

func (z *Zedmon) Stop() error {
	if z == nil {
		return z.fail(errors.New("Nil zedmon"))
	}
	if z.cmd == nil {
		return z.fail(errors.New("No zedmon command"))
	}
	if z.cmd.Process == nil {
		return z.fail(errors.New("No zedmon process"))
	}
	err := z.cmd.Process.Signal(syscall.SIGINT)
	if err != nil {
		return z.fail(errors.New("Failed to send zedmon process SIGINT"))
	}
	z.stdoutMutex.Lock()
	err = z.cmd.Wait()
	z.cmd = nil
	z.stdoutMutex.Unlock()

	z.stderr = nil
	return err
}
