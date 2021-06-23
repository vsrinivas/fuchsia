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
	var baseInfo network.DeviceInfo
	baseInfo.SetMaxBufferLength(16 * 1024)
	baseInfo.SetMinRxBufferLength(0)
	baseInfo.SetRxDepth(16)
	baseInfo.SetTxDepth(16)
	baseInfo.SetBufferAlignment(1)

	factory := SimpleSessionConfigFactory{
		FrameTypes: []network.FrameType{network.FrameTypeEthernet},
	}

	tests := []struct {
		name       string
		updateInfo func(info *network.DeviceInfo)
		expect     SessionConfig
	}{
		{
			name:       "defaults",
			updateInfo: func(info *network.DeviceInfo) {},
			expect: SessionConfig{
				BufferLength:      DefaultBufferLength,
				BufferStride:      DefaultBufferLength,
				DescriptorLength:  descriptorLength,
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
			expect: SessionConfig{
				BufferLength:      DefaultBufferLength / 4,
				BufferStride:      DefaultBufferLength / 4,
				DescriptorLength:  descriptorLength,
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
			expect: SessionConfig{
				BufferLength:      DefaultBufferLength * 2,
				BufferStride:      DefaultBufferLength * 2,
				DescriptorLength:  descriptorLength,
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
			expect: SessionConfig{
				BufferLength:      DefaultBufferLength + 112,
				BufferStride:      DefaultBufferLength + 128,
				DescriptorLength:  descriptorLength,
				RxDescriptorCount: baseInfo.TxDepth,
				TxDescriptorCount: baseInfo.RxDepth,
				Options:           network.SessionFlagsPrimary,
				RxFrames:          factory.FrameTypes,
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			info := baseInfo
			test.updateInfo(&info)
			sessionInfo, err := factory.MakeSessionConfig(&info)
			if err != nil {
				t.Fatalf("MakeSessionConfig(%+v): %s", info, err)
			}
			if diff := cmp.Diff(test.expect, sessionInfo); diff != "" {
				t.Errorf("MakeSessionConfig(%+v): (-want +got)\n%s", info, diff)
			}
		})
	}
}
