// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/base64"
	"flag"
	"fmt"
	"math/rand"
	"os"
	"path"
	"reflect"
	"strings"
	"testing"
	"time"

	fg "go.fuchsia.dev/fuchsia/tools/femu-control/femu-grpc"
)

type keyEvent struct {
	evType string
	keySeq []string
}

type fakeFemuGrpcClient struct {
	keyEvents []keyEvent
}

func (f *fakeFemuGrpcClient) StreamScreen(opts fg.StreamScreenOpts) (fg.Frames, error) {
	numFrames := opts.NumFrames
	if numFrames == 0 || numFrames > 10 {
		numFrames = 10
	}

	frames := fg.Frames{}
	frames.Width = 1
	frames.Height = 1

	const png1x1Base64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQYV2PwUSn4DwADgAHgUix5GgAAAABJRU5ErkJggg=="
	pngBytes, _ := base64.StdEncoding.DecodeString(png1x1Base64)
	for i := uint(0); i < numFrames; i++ {
		frames.Images = append(frames.Images, pngBytes)
	}

	return frames, nil
}

func (f *fakeFemuGrpcClient) StreamAudio(opts fg.StreamAudioOpts) (fg.AudioPayload, error) {
	audio := fg.AudioPayload{}

	byteSize := uint(opts.Duration.Milliseconds()) * opts.Format.SamplingRate / 1000 * opts.Format.BitsPerSample / 8
	if opts.Format.Channels == "STEREO" {
		byteSize = byteSize * 2
	}

	audio.Bytes = make([]byte, byteSize)
	audio.LengthMs = uint(opts.Duration.Milliseconds())
	return audio, nil
}

func (f *fakeFemuGrpcClient) KeyDown(seq []string) error {
	f.keyEvents = append(f.keyEvents, keyEvent{
		evType: "DOWN",
		keySeq: seq,
	})
	return nil
}

func (f *fakeFemuGrpcClient) KeyUp(seq []string) error {
	f.keyEvents = append(f.keyEvents, keyEvent{
		evType: "UP",
		keySeq: seq,
	})
	return nil
}

func (f *fakeFemuGrpcClient) KeyPress(seq []string) error {
	f.keyEvents = append(f.keyEvents, keyEvent{
		evType: "PRESS",
		keySeq: seq,
	})
	return nil
}

func TestStreamScreen(t *testing.T) {
	client := fakeFemuGrpcClient{}
	ctx := context.WithValue(context.Background(), "client", &client)

	var seededRand *rand.Rand = rand.New(
		rand.NewSource(time.Now().UnixNano()))
	tmpDir := fmt.Sprintf("/tmp/femu%v", seededRand.Int())

	testCases := []struct {
		name           string
		flags          string
		expectedStatus string
		expectedError  string
		setup          func()
		cleanup        func()
	}{
		{
			name:           "num-frames and duration both missing",
			flags:          fmt.Sprintf("-out %v/screen%%.png", tmpDir),
			expectedStatus: "success",
			cleanup:        func() { os.RemoveAll(tmpDir) },
		},
		{
			name:           "path is not directory",
			flags:          fmt.Sprintf("-duration 1s -out %v/file/screen%%.png", tmpDir),
			expectedStatus: "usage error",
			expectedError:  fmt.Sprintf("path %v/file/ invalid: stat %v/file/: not a directory", tmpDir, tmpDir),
			setup: func() {
				os.MkdirAll(tmpDir, 0755)
				os.Create(path.Join(tmpDir, "file"))
			},
			cleanup: func() {
				os.Remove(path.Join(tmpDir, "file"))
			},
		},
		{
			name:           "filename not containing extension",
			flags:          fmt.Sprintf("-duration 1s -out %v/screen%%", tmpDir),
			expectedStatus: "usage error",
			expectedError:  "filename 'screen%' doesn't contain any extension!",
		},
		{
			name:           "filename not containing %",
			flags:          fmt.Sprintf("-duration 1s -out %v/screen.jpg", tmpDir),
			expectedStatus: "success",
		},
		{
			name:           "unsupported extension",
			flags:          fmt.Sprintf("-duration 1s -out %v/screen%%.wav", tmpDir),
			expectedStatus: "usage error",
			expectedError:  "filename 'screen%.wav' has unsupported extension: .wav. supported extensions are jpeg, jpg, gif, and png.",
		},
		{
			name:           "valid usage: jpg",
			flags:          fmt.Sprintf("-duration 1s -out %v/screen%%.jpg", tmpDir),
			expectedStatus: "success",
			cleanup:        func() { os.RemoveAll(tmpDir) },
		},
		{
			name:           "valid usage: png",
			flags:          fmt.Sprintf("-duration 1s -out %v/screen%%.png", tmpDir),
			expectedStatus: "success",
			cleanup:        func() { os.RemoveAll(tmpDir) },
		},
		{
			name:           "valid usage: gif",
			flags:          fmt.Sprintf("-duration 1s -out %v/screen.gif", tmpDir),
			expectedStatus: "success",
			cleanup:        func() { os.RemoveAll(tmpDir) },
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			if tc.setup != nil {
				tc.setup()
			}
			if tc.cleanup != nil {
				defer tc.cleanup()
			}

			cmd := recordScreenCmd{}

			f := flag.NewFlagSet("", flag.ContinueOnError)
			cmd.SetFlags(f)

			err := f.Parse(strings.Split(tc.flags, " "))
			if err != nil {
				t.Errorf("parse flags failed: %v", err)
				return
			}

			err = cmd.ValidateArgs()
			switch {
			case err == nil && tc.expectedStatus == "usage error":
				t.Errorf("(usage error) expected: %v actual: %v", tc.expectedError, err)
			case err != nil && tc.expectedStatus == "usage error" && tc.expectedError != err.Error():
				t.Errorf("(usage error) expected: %v actual: %v", tc.expectedError, err)
				return
			case err != nil && tc.expectedStatus != "usage error":
				t.Errorf("expected: %v actual (usage error): %v", tc.expectedStatus, err)
				return
			case err != nil:
				return
			}

			err = cmd.run(ctx)
			if tc.expectedStatus == "failure" {
				if err == nil || err.Error() != tc.expectedError {
					t.Errorf("expected: %v actual: %v", tc.expectedError, err)
				}
			}

			if tc.expectedStatus == "success" {
				if err != nil {
					t.Errorf("expected: success actual: %v", err)
				}
			}
		})
	}
}

func TestStreamAudio(t *testing.T) {
	client := fakeFemuGrpcClient{}
	ctx := context.WithValue(context.Background(), "client", &client)

	var seededRand *rand.Rand = rand.New(
		rand.NewSource(time.Now().UnixNano()))
	tmpDir := fmt.Sprintf("/tmp/femu%v", seededRand.Int())

	testCases := []struct {
		name           string
		flags          string
		expectedStatus string
		expectedError  string
		setup          func()
		cleanup        func()
	}{
		{
			name:           "-duration missing",
			flags:          fmt.Sprintf("-out %v/audio.wav", tmpDir),
			expectedStatus: "usage error",
			expectedError:  "-duration cannot be zero",
		},
		{
			name:           "path is not directory",
			flags:          fmt.Sprintf("-duration 1s -out %v/file/audio.wav", tmpDir),
			expectedStatus: "usage error",
			expectedError:  fmt.Sprintf("path %v/file/ invalid: stat %v/file/: not a directory", tmpDir, tmpDir),
			setup: func() {
				os.MkdirAll(tmpDir, 0755)
				os.Create(path.Join(tmpDir, "file"))
			},
			cleanup: func() {
				os.Remove(path.Join(tmpDir, "file"))
			},
		},
		{
			name:           "invalid args: channels",
			flags:          fmt.Sprintf("-duration 1s -out %v/audio.wav -channels SURROUND", tmpDir),
			expectedStatus: "usage error",
			expectedError:  "unsupported channel format: SURROUND",
		},
		{
			name:           "invalid args: bit depth",
			flags:          fmt.Sprintf("-duration 1s -out %v/audio.wav -bit-depth 32", tmpDir),
			expectedStatus: "usage error",
			expectedError:  "unsupported number of bits per sample: 32",
		},
		{
			name:           "invalid args: sampling rate",
			flags:          fmt.Sprintf("-duration 1s -out %v/audio.wav -sampling-rate 0", tmpDir),
			expectedStatus: "usage error",
			expectedError:  "invalid sampling rate: 0",
		},
		{
			name:           "valid usage",
			flags:          fmt.Sprintf("-duration 1s -out %v/screen.wav", tmpDir),
			expectedStatus: "success",
			cleanup:        func() { os.RemoveAll(tmpDir) },
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			if tc.setup != nil {
				tc.setup()
			}
			if tc.cleanup != nil {
				defer tc.cleanup()
			}

			cmd := recordAudioCmd{}

			f := flag.NewFlagSet("", flag.ContinueOnError)
			cmd.SetFlags(f)

			err := f.Parse(strings.Split(tc.flags, " "))
			if err != nil {
				t.Errorf("parse flags failed: %v", err)
				return
			}

			err = cmd.ValidateArgs()
			switch {
			case err == nil && tc.expectedStatus == "usage error":
				t.Errorf("(usage error) expected: %v actual: %v", tc.expectedError, err)
			case err != nil && tc.expectedStatus == "usage error" && tc.expectedError != err.Error():
				t.Errorf("(usage error) expected: %v actual: %v", tc.expectedError, err)
				return
			case err != nil && tc.expectedStatus != "usage error":
				t.Errorf("expected: %v actual (usage error): %v", tc.expectedStatus, err)
				return
			case err != nil:
				return
			}

			err = cmd.run(ctx)
			if tc.expectedStatus == "failure" {
				if err == nil || err.Error() != tc.expectedError {
					t.Errorf("expected: %v actual: %v", tc.expectedError, err)
				}
			}

			if tc.expectedStatus == "success" {
				if err != nil {
					t.Errorf("expected: success actual: %v", err)
				}
			}
		})
	}
}

func TestKeyboard(t *testing.T) {
	client := fakeFemuGrpcClient{}
	ctx := context.WithValue(context.Background(), "client", &client)

	testCases := []struct {
		name           string
		flags          string
		expectedStatus string
		expectedError  string
		setup          func()
		cleanup        func()
	}{
		{
			name:           "invalid type",
			flags:          "-event OTHER Ctrl+Alt+A",
			expectedStatus: "usage error",
			expectedError:  "unknown event type: OTHER",
		},
		{
			name:           "key sequence missing",
			flags:          "-event PRESS",
			expectedStatus: "usage error",
			expectedError:  "key sequence missing",
		},
		{
			name:           "key down",
			flags:          "-event DOWN Control+Alt+A",
			expectedStatus: "success",
			expectedError:  "",
			cleanup: func() {
				if len(client.keyEvents) == 0 {
					t.Fatalf("client.keyEvents is empty")
				}
				event := client.keyEvents[len(client.keyEvents)-1]
				if event.evType != "DOWN" {
					t.Errorf("incorrect event.evType: expected: %v actual: %v", "DOWN", event.evType)
				}

				expectedKeySeq := []string{"Control", "Alt", "A"}
				if !reflect.DeepEqual(event.keySeq, expectedKeySeq) {
					t.Errorf("incorrect event.keySeq: expected: %v actual: %v", expectedKeySeq, event.keySeq)
				}
			},
		},
		{
			name:           "key up",
			flags:          "-event UP Alt+Shift+3",
			expectedStatus: "success",
			expectedError:  "",
			cleanup: func() {
				if len(client.keyEvents) == 0 {
					t.Fatalf("client.keyEvents is empty")
				}
				event := client.keyEvents[len(client.keyEvents)-1]
				if event.evType != "UP" {
					t.Errorf("incorrect event.evType: expected: %v actual: %v", "UP", event.evType)
				}

				expectedKeySeq := []string{"Alt", "Shift", "3"}
				if !reflect.DeepEqual(event.keySeq, expectedKeySeq) {
					t.Errorf("incorrect event.keySeq: expected: %v actual: %v", expectedKeySeq, event.keySeq)
				}
			},
		},
		{
			name:           "key press",
			flags:          "-event PRESS Meta+Z",
			expectedStatus: "success",
			expectedError:  "",
			cleanup: func() {
				if len(client.keyEvents) == 0 {
					t.Fatalf("client.keyEvents is empty")
				}
				event := client.keyEvents[len(client.keyEvents)-1]
				if event.evType != "PRESS" {
					t.Errorf("incorrect event.evType: expected: %v actual: %v", "PRESS", event.evType)
				}

				expectedKeySeq := []string{"Meta", "Z"}
				if !reflect.DeepEqual(event.keySeq, expectedKeySeq) {
					t.Errorf("incorrect event.keySeq: expected: %v actual: %v", expectedKeySeq, event.keySeq)
				}
			},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			if tc.setup != nil {
				tc.setup()
			}
			if tc.cleanup != nil {
				defer tc.cleanup()
			}

			cmd := keyboardCmd{}

			f := flag.NewFlagSet("", flag.ContinueOnError)
			cmd.SetFlags(f)

			err := f.Parse(strings.Split(tc.flags, " "))
			if err != nil {
				t.Errorf("parse flags failed: %v", err)
				return
			}
			if f.Arg(0) != "" {
				cmd.keySequence = strings.Split(f.Arg(0), "+")
			}

			err = cmd.ValidateArgs()
			switch {
			case err == nil && tc.expectedStatus == "usage error":
				t.Errorf("(usage error) expected: %v actual: %v", tc.expectedError, err)
			case err != nil && tc.expectedStatus == "usage error" && tc.expectedError != err.Error():
				t.Errorf("(usage error) expected: %v actual: %v", tc.expectedError, err)
				return
			case err != nil && tc.expectedStatus != "usage error":
				t.Errorf("expected: %v actual (usage error): %v", tc.expectedStatus, err)
				return
			case err != nil:
				return
			}

			err = cmd.run(ctx)
			if tc.expectedStatus == "failure" {
				if err == nil || err.Error() != tc.expectedError {
					t.Errorf("expected: %v actual: %v", tc.expectedError, err)
				}
			}

			if tc.expectedStatus == "success" {
				if err != nil {
					t.Errorf("expected: success actual: %v", err)
				}
			}
		})
	}
}
