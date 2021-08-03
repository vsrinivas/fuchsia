// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package femu_grpc

import (
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	pb "go.fuchsia.dev/fuchsia/tools/femu-control/femu-grpc/proto"
	grpc "google.golang.org/grpc"
	codes "google.golang.org/grpc/codes"
)

type FemuGrpcClientConfig struct {
	ServerAddr string
	Timeout    time.Duration
}

type FemuGrpcClientInterface interface {
	StreamScreen(opts StreamScreenOpts) (Frames, error)
	StreamAudio(opts StreamAudioOpts) (AudioPayload, error)
	KeyDown(seq []string) error
	KeyUp(seq []string) error
	KeyPress(seq []string) error
}

type FemuGrpcClient struct {
	conn   *grpc.ClientConn
	client pb.EmulatorControllerClient
	config FemuGrpcClientConfig
}

func NewFemuGrpcClient(config FemuGrpcClientConfig) (FemuGrpcClient, error) {
	conn, err := grpc.Dial(config.ServerAddr, grpc.WithInsecure())
	if err != nil {
		log.Fatalln("fail to dial: ", err)
		return FemuGrpcClient{}, err
	}

	client := pb.NewEmulatorControllerClient(conn)
	return FemuGrpcClient{
		conn:   conn,
		client: client,
		config: config,
	}, nil
}

type StreamScreenOpts struct {
	Format    string
	NumFrames uint
	Duration  time.Duration
}

func NewStreamScreenOpts(format string, numFrames uint, duration time.Duration) (StreamScreenOpts, error) {
	if format == "" {
		format = "PNG"
	}

	switch format {
	case "PNG", "RGB", "RGBA":
		break
	default:
		return StreamScreenOpts{}, fmt.Errorf("wrong options: invalid format %v, supported formats: PNG, RGB, RGBA", format)
	}

	return StreamScreenOpts{
		Format:    format,
		NumFrames: numFrames,
		Duration:  duration,
	}, nil
}

type Frames struct {
	Images [][]byte
	Width  uint
	Height uint
}

func (emu *FemuGrpcClient) StreamScreen(opts StreamScreenOpts) (Frames, error) {
	frames := Frames{}

	var format pb.ImageFormat_ImgFormat
	switch opts.Format {
	case "PNG":
		format = pb.ImageFormat_PNG
	case "RGB":
		format = pb.ImageFormat_RGB888
	case "RGBA":
		format = pb.ImageFormat_RGBA8888
	}

	imageFormat := pb.ImageFormat{
		Format: format,
		Rotation: &pb.Rotation{
			Rotation: pb.Rotation_PORTRAIT,
		},
	}

	var (
		ctx    context.Context
		cancel context.CancelFunc
	)
	if opts.Duration > 0 {
		ctx, cancel = context.WithTimeout(context.Background(), opts.Duration)
	} else {
		ctx, cancel = context.WithCancel(context.Background())
	}
	defer cancel()

	stream, err := emu.client.StreamScreenshot(ctx, &imageFormat)
	if err != nil {
		return frames, err
	}

	signalChannel := make(chan os.Signal, 2)
	signal.Notify(signalChannel, os.Interrupt, syscall.SIGINT)
	go func() {
		<-signalChannel
		cancel()
	}()

	for {
		screenshot, err := stream.Recv()
		if err == io.EOF || grpc.Code(err) == codes.DeadlineExceeded {
			break
		}
		if err != nil {
			return frames, err
		}

		frames.Images = append(frames.Images, screenshot.GetImage())
		frames.Height = uint(screenshot.GetFormat().GetHeight())
		frames.Width = uint(screenshot.GetFormat().GetWidth())

		if opts.NumFrames > 0 && len(frames.Images) == int(opts.NumFrames) {
			break
		}
	}

	return frames, nil
}

type AudioFormat struct {
	Channels      string // "MONO" or "STEREO"
	BitsPerSample uint   // 8 or 16
	SamplingRate  uint
}

func (f *AudioFormat) Validate() error {
	switch f.Channels {
	case "MONO", "STEREO":
		break
	default:
		return fmt.Errorf("unsupported channel format: %s", f.Channels)
	}

	switch f.BitsPerSample {
	case 8, 16:
		break
	default:
		return fmt.Errorf("unsupported number of bits per sample: %d", f.BitsPerSample)
	}

	if f.SamplingRate == 0 {
		return fmt.Errorf("invalid sampling rate: %d", f.SamplingRate)

	}
	return nil
}

type StreamAudioOpts struct {
	Format   AudioFormat
	Duration time.Duration
}

func NewStreamAudioOpts(channels string, bitsPerSample uint, samplingRate uint, duration time.Duration) (StreamAudioOpts, error) {
	// Fill default value for each field
	if channels == "" {
		channels = "STEREO"
	}

	if bitsPerSample == 0 {
		bitsPerSample = 16
	}

	if samplingRate == 0 {
		samplingRate = 44100
	}

	// Validation
	if duration == 0 {
		return StreamAudioOpts{}, fmt.Errorf("invalid streaming duration: %v", duration)
	}

	switch channels {
	case "MONO", "STEREO":
		break
	default:
		return StreamAudioOpts{}, fmt.Errorf("unsupported channel format: %s", channels)
	}

	switch bitsPerSample {
	case 8, 16:
		break
	default:
		return StreamAudioOpts{}, fmt.Errorf("unsupported number of bits per sample: %d", bitsPerSample)
	}

	if samplingRate == 0 {
		return StreamAudioOpts{}, fmt.Errorf("invalid sampling rate: %d", samplingRate)

	}

	return StreamAudioOpts{
		Format: AudioFormat{
			Channels:      channels,
			BitsPerSample: bitsPerSample,
			SamplingRate:  samplingRate,
		},
		Duration: duration,
	}, nil
}

type AudioPayload struct {
	Bytes    []byte
	LengthMs uint
}

func (emu *FemuGrpcClient) StreamAudio(opts StreamAudioOpts) (AudioPayload, error) {
	payload := AudioPayload{}

	var channels pb.AudioFormat_Channels
	switch opts.Format.Channels {
	case "MONO":
		channels = pb.AudioFormat_Mono
	case "STEREO":
		channels = pb.AudioFormat_Stereo
	}

	var sampleFormat pb.AudioFormat_SampleFormat
	switch opts.Format.BitsPerSample {
	case 8:
		sampleFormat = pb.AudioFormat_AUD_FMT_U8
	case 16:
		sampleFormat = pb.AudioFormat_AUD_FMT_S16
	}

	audioFormat := pb.AudioFormat{
		SamplingRate: uint64(opts.Format.SamplingRate),
		Channels:     channels,
		Format:       sampleFormat,
	}

	timeout := opts.Duration + emu.config.Timeout
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	stream, err := emu.client.StreamAudio(ctx, &audioFormat)
	if err != nil {
		return payload, err
	}

	startTimestamp := uint64(0)
	for payload.LengthMs < uint(opts.Duration.Milliseconds()) {
		audioPacket, err := stream.Recv()
		if err == io.EOF || grpc.Code(err) == codes.DeadlineExceeded {
			break
		}
		if err != nil {
			return payload, err
		}

		timestampMs := audioPacket.GetTimestamp() / 1000 // us to ms
		if startTimestamp == 0 {
			startTimestamp = timestampMs
		}

		payload.LengthMs = uint(timestampMs - startTimestamp)
		payload.Bytes = append(payload.Bytes, audioPacket.GetAudio()...)
	}
	return payload, nil
}

func (emu *FemuGrpcClient) keyOps(seq []string, eventType pb.KeyboardEvent_KeyEventType) error {
	timeout := emu.config.Timeout
	for i := 0; i < len(seq); i++ {
		ctx, cancel := context.WithTimeout(context.Background(), timeout)
		defer cancel()

		keyText := seq[i]
		if len(keyText) == 1 {
			keyText = strings.ToLower(keyText)
		}
		if keyText == "Ctrl" {
			keyText = "Control"
		}

		key := pb.KeyboardEvent{
			EventType: eventType,
			Key:       keyText,
		}
		_, err := emu.client.SendKey(ctx, &key)
		if err != nil {
			return err
		}
	}
	return nil
}

func (emu *FemuGrpcClient) KeyDown(seq []string) error {
	return emu.keyOps(seq, pb.KeyboardEvent_keydown)
}

func (emu *FemuGrpcClient) KeyUp(seq []string) error {
	return emu.keyOps(seq, pb.KeyboardEvent_keydown)
}

func (emu *FemuGrpcClient) KeyPress(seq []string) error {
	reversedSeq := make([]string, len(seq))
	for i := len(seq) - 1; i >= 0; i-- {
		reversedSeq[len(seq)-1-i] = seq[i]
	}
	if err := emu.keyOps(seq, pb.KeyboardEvent_keydown); err != nil {
		return err
	}
	return emu.keyOps(reversedSeq, pb.KeyboardEvent_keyup)
}
