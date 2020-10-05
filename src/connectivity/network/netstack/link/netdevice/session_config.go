// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netdevice

import "fidl/fuchsia/hardware/network"

// A factory of session configurations from device information.
// A default implementation is provided by SimpleSessionConfigFactory.
type SessionConfigFactory interface {
	// Creates a SessionConfig for a given network device based on the provided
	// deviceInfo.
	MakeSessionConfig(deviceInfo *network.Info) (SessionConfig, error)
}

// Configuration used to open a session with a network device.
type SessionConfig struct {
	// Length of each buffer.
	BufferLength uint32
	// Buffer stride on VMO.
	BufferStride uint32
	// Descriptor length, in bytes.
	DescriptorLength uint64
	// Number of rx descriptors to allocate.
	RxDescriptorCount uint16
	// Number of tx descriptors to allocate.
	TxDescriptorCount uint16
	// Session flags.
	Options network.SessionFlags
	// Types of rx frames to subscribe to.
	RxFrames []network.FrameType
}

// The buffer length used by SimpleSessionConfigFactory.
const DefaultBufferLength uint32 = 2048

// A simple session configuration factory.
type SimpleSessionConfigFactory struct {
	// The frame types to subscribe to. Will subscribe to all frame types if
	// empty.
	FrameTypes []network.FrameType
}

// MakeSessionConfig implements SessionConfigFactory.
func (c *SimpleSessionConfigFactory) MakeSessionConfig(deviceInfo *network.Info) (SessionConfig, error) {
	bufferLength := DefaultBufferLength
	if deviceInfo.MaxBufferLength < bufferLength {
		bufferLength = deviceInfo.MaxBufferLength
	}
	frameTypes := c.FrameTypes
	if len(frameTypes) == 0 {
		frameTypes = deviceInfo.RxTypes
	}

	config := SessionConfig{
		BufferLength:      bufferLength,
		BufferStride:      bufferLength,
		DescriptorLength:  descriptorLength,
		RxDescriptorCount: deviceInfo.RxDepth,
		TxDescriptorCount: deviceInfo.TxDepth,
		Options:           network.SessionFlagsPrimary,
		RxFrames:          frameTypes,
	}
	align := deviceInfo.BufferAlignment
	if config.BufferStride%align != 0 {
		// Align back.
		config.BufferStride -= config.BufferStride % align
		// Align up if we have space.
		if config.BufferStride+align <= deviceInfo.MaxBufferLength {
			config.BufferStride += align
		}
	}
	return config, nil
}
