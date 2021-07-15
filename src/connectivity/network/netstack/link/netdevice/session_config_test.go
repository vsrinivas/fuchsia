// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netdevice

import (
	"testing"

	"fidl/fuchsia/hardware/network"

	"github.com/google/go-cmp/cmp"
)

func TestMakeSessionConfig(t *testing.T) {
	const txBufferHead = 16
	const txBufferTail = 8
	var baseInfo network.DeviceInfo
	baseInfo.SetMaxBufferLength(16 * 1024)
	baseInfo.SetMinRxBufferLength(0)
	baseInfo.SetRxDepth(16)
	baseInfo.SetTxDepth(16)
	baseInfo.SetBufferAlignment(1)
	baseInfo.SetMinTxBufferHead(txBufferHead)
	baseInfo.SetMinTxBufferTail(txBufferTail)

	factory := SimpleSessionConfigFactory{
		FrameTypes: []network.FrameType{network.FrameTypeEthernet},
	}

	tests := []struct {
		name             string
		updateInfo       func(*network.DeviceInfo)
		updatePortStatus func(*network.PortStatus)
		expectedConfig   SessionConfig
		expectedErr      *InsufficientBufferLengthError
	}{
		{
			name: "defaults",
			expectedConfig: SessionConfig{
				BufferLength:      DefaultBufferLength,
				BufferStride:      DefaultBufferLength,
				DescriptorLength:  descriptorLength,
				TxHeaderLength:    baseInfo.MinTxBufferHead,
				TxTailLength:      baseInfo.MinTxBufferTail,
				RxDescriptorCount: baseInfo.TxDepth,
				TxDescriptorCount: baseInfo.RxDepth,
				Options:           network.SessionFlagsPrimary,
				RxFrames:          factory.FrameTypes,
			},
		},
		{
			name: "respect max buffer length",
			updateInfo: func(info *network.DeviceInfo) {
				info.SetMaxBufferLength(DefaultBufferLength / 4)
			},
			expectedConfig: SessionConfig{
				BufferLength:      DefaultBufferLength / 4,
				BufferStride:      DefaultBufferLength / 4,
				DescriptorLength:  descriptorLength,
				TxHeaderLength:    baseInfo.MinTxBufferHead,
				TxTailLength:      baseInfo.MinTxBufferTail,
				RxDescriptorCount: baseInfo.TxDepth,
				TxDescriptorCount: baseInfo.RxDepth,
				Options:           network.SessionFlagsPrimary,
				RxFrames:          factory.FrameTypes,
			},
		},
		{
			name: "respect min buffer length",
			updateInfo: func(info *network.DeviceInfo) {
				info.SetMinRxBufferLength(DefaultBufferLength * 2)
			},
			expectedConfig: SessionConfig{
				BufferLength:      DefaultBufferLength * 2,
				BufferStride:      DefaultBufferLength * 2,
				DescriptorLength:  descriptorLength,
				TxHeaderLength:    baseInfo.MinTxBufferHead,
				TxTailLength:      baseInfo.MinTxBufferTail,
				RxDescriptorCount: baseInfo.TxDepth,
				TxDescriptorCount: baseInfo.RxDepth,
				Options:           network.SessionFlagsPrimary,
				RxFrames:          factory.FrameTypes,
			},
		},
		{
			name: "buffer alignment",
			updateInfo: func(info *network.DeviceInfo) {
				info.SetBufferAlignment(64)
				info.SetMinRxBufferLength(DefaultBufferLength + 112)
			},
			expectedConfig: SessionConfig{
				BufferLength:      DefaultBufferLength + 112,
				BufferStride:      DefaultBufferLength + 128,
				DescriptorLength:  descriptorLength,
				TxHeaderLength:    baseInfo.MinTxBufferHead,
				TxTailLength:      baseInfo.MinTxBufferTail,
				RxDescriptorCount: baseInfo.TxDepth,
				TxDescriptorCount: baseInfo.RxDepth,
				Options:           network.SessionFlagsPrimary,
				RxFrames:          factory.FrameTypes,
			},
		},
		{
			name: "insufficient buffer length",
			updatePortStatus: func(portStatus *network.PortStatus) {
				portStatus.SetMtu(DefaultBufferLength)
			},
			expectedErr: &InsufficientBufferLengthError{
				BufferLength: DefaultBufferLength,
				BufferHeader: txBufferHead,
				BufferTail:   txBufferTail,
				MTU:          DefaultBufferLength,
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			info := baseInfo
			if test.updateInfo != nil {
				test.updateInfo(&info)
			}
			var portStatus network.PortStatus
			if test.updatePortStatus != nil {
				test.updatePortStatus(&portStatus)
			}
			sessionConfig, err := factory.MakeSessionConfig(info, portStatus)
			if test.expectedErr != nil {
				if concreteErr, ok := err.(*InsufficientBufferLengthError); ok {
					if diff := cmp.Diff(concreteErr, test.expectedErr); diff != "" {
						t.Errorf("MakeSessionConfig(%+v, %+v) error diff: (-want +got)\n%s", info, portStatus, diff)
					}
				} else {
					t.Fatalf("got MakeSessionConfig(%+v, %+v) error = %+v, want %s", info, portStatus, err, test.expectedErr)
				}
			} else {
				if err != nil {
					t.Fatalf("MakeSessionConfig(%+v, %+v): %s", info, portStatus, err)
				}
				if diff := cmp.Diff(test.expectedConfig, sessionConfig); diff != "" {
					t.Errorf("MakeSessionConfig(%+v, %+v): (-want +got)\n%s", info, portStatus, diff)
				}
			}
		})
	}
}
