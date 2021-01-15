// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"syscall"
	"time"

	"github.com/google/subcommands"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"

	fg "go.fuchsia.dev/fuchsia/tools/femu-control/femu-grpc"
)

type recordAudioCmd struct {
	channels     string
	bitDepth     uint
	samplingRate uint
	duration     time.Duration
	audioOutput  string
}

func (*recordAudioCmd) Name() string     { return "record_audio" }
func (*recordAudioCmd) Synopsis() string { return "Record audio to a PCM wave file" }

func (*recordAudioCmd) Usage() string {
	return `record_audio: Record FEMU audio to a PCM WAV file.

Usage:

record_audio -duration <d> [-channels MONO|STEREO] [-bit-depth 8|16]
        [-sampling-rate <r>] [-out dir/file.wav]

Flags:
`
}

func (c *recordAudioCmd) SetFlags(f *flag.FlagSet) {
	f.StringVar(&c.channels, "channels", "STEREO", "audio channels: MONO or STEREO")
	f.UintVar(&c.bitDepth, "bit-depth", 16, "audio bit depth: 8 or 16")
	f.UintVar(&c.samplingRate, "sampling-rate", 44100, "sampling rate")
	f.DurationVar(&c.duration, "duration", time.Duration(0), "recording duration")
	f.StringVar(&c.audioOutput, "out", "./audio.wav", "audio file output path")
}

func (c *recordAudioCmd) ValidateArgs() error {
	if c.audioOutput == "" {
		return fmt.Errorf("-out flag is required")
	}

	if c.duration == 0 {
		return fmt.Errorf("-duration cannot be zero")
	}

	opts := fg.AudioFormat{
		Channels:      c.channels,
		BitsPerSample: c.bitDepth,
		SamplingRate:  c.samplingRate,
	}
	if err := opts.Validate(); err != nil {
		return err
	}

	var err error = nil
	c.audioOutput, err = filepath.Abs(c.audioOutput)
	if err != nil {
		return fmt.Errorf("cannot get absolute path of '%s': %v", c.audioOutput, err)
	}
	dir, _ := path.Split(c.audioOutput)
	fileInfo, err := os.Stat(dir)
	switch {
	case err != nil && !os.IsNotExist(err):
		return fmt.Errorf("path %s invalid: %v", dir, err)
	case err == nil && !fileInfo.IsDir():
		return fmt.Errorf("path %s is not a directory", dir)
	case err == nil && fileInfo.IsDir() && syscall.Access(dir, syscall.O_RDWR) != nil:
		return fmt.Errorf("path %s is not writeable", dir)
	}

	return nil
}

func (c *recordAudioCmd) generatePcmHeader(payload *[]byte) []byte {
	buf := new(bytes.Buffer)

	chunkId := []byte("RIFF")
	chunkSize := uint32(36 + len(*payload))
	format := []byte("WAVE")

	subchunk1Id := []byte("fmt ")
	subchunk1Size := uint32(16)
	audioFormat := uint16(1)
	numChannels := uint16(1)
	if c.channels == "STEREO" {
		numChannels = 2
	}
	sampleRate := uint32(c.samplingRate)
	byteRate := uint32(sampleRate * uint32(numChannels) * uint32(c.bitDepth) / 8)
	blockAlign := uint16(numChannels * uint16(c.bitDepth) / 8)
	bitsPerSample := uint16(c.bitDepth)

	subchunk2Id := []byte("data")
	subchunk2Size := uint32(len(*payload))

	binary.Write(buf, binary.LittleEndian, &chunkId)
	binary.Write(buf, binary.LittleEndian, &chunkSize)
	binary.Write(buf, binary.LittleEndian, &format)

	binary.Write(buf, binary.LittleEndian, &subchunk1Id)
	binary.Write(buf, binary.LittleEndian, &subchunk1Size)
	binary.Write(buf, binary.LittleEndian, &audioFormat)
	binary.Write(buf, binary.LittleEndian, &numChannels)

	binary.Write(buf, binary.LittleEndian, &sampleRate)
	binary.Write(buf, binary.LittleEndian, &byteRate)
	binary.Write(buf, binary.LittleEndian, &blockAlign)
	binary.Write(buf, binary.LittleEndian, &bitsPerSample)

	binary.Write(buf, binary.LittleEndian, &subchunk2Id)
	binary.Write(buf, binary.LittleEndian, &subchunk2Size)

	return buf.Bytes()
}

func (c *recordAudioCmd) run(ctx context.Context) error {
	dir, _ := path.Split(c.audioOutput)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("error while creating directory %s: %v", dir, err)
	}

	f, err := os.Create(c.audioOutput)
	defer f.Close()
	if err != nil {
		return fmt.Errorf("error while creating file %s: %v", c.audioOutput, err)
	}

	opts, err := fg.NewStreamAudioOpts(c.channels, c.bitDepth, c.samplingRate, c.duration)
	if err != nil {
		return fmt.Errorf("cannot create StreamAudioOpts: %v", err)
	}

	client := ctx.Value("client").(fg.FemuGrpcClientInterface)
	if client == nil {
		return fmt.Errorf("FEMU gRPC client not found")
	}

	audio, err := client.StreamAudio(opts)
	if err != nil {
		return fmt.Errorf("StreamAudio error: %v", err)
	}

	header := c.generatePcmHeader(&audio.Bytes)

	if _, err = f.Write(header); err != nil {
		return fmt.Errorf("error while writing PCM header: %v", err)
	}

	if _, err = f.Write(audio.Bytes); err != nil {
		return fmt.Errorf("error while writing PCM payload: %v", err)
	}

	return nil
}

func (c *recordAudioCmd) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if err := c.ValidateArgs(); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitUsageError
	}

	if err := c.run(ctx); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
