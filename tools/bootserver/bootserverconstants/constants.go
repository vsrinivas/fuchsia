// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserverconstants

import (
	"fmt"
)

const (
	// Special image names recognized by fuchsia's netsvc.
	AuthorizedKeysNetsvcName = "<<image>>authorized_keys"
	BoardInfoNetsvcName      = "<<image>>board_info"
	BootloaderNetsvcName     = "<<image>>bootloader.img"
	CmdlineNetsvcName        = "<<netboot>>cmdline"
	EfiNetsvcName            = "<<image>>efi.img"
	FirmwareNetsvcPrefix     = "<<image>>firmware_"
	FvmNetsvcName            = "<<image>>sparse.fvm"
	KerncNetsvcName          = "<<image>>kernc.img"
	KernelNetsvcName         = "<<netboot>>kernel.bin"
	VbmetaANetsvcName        = "<<image>>vbmetaa.img"
	VbmetaBNetsvcName        = "<<image>>vbmetab.img"
	VbmetaRNetsvcName        = "<<image>>vbmetar.img"
	ZirconANetsvcName        = "<<image>>zircona.img"
	ZirconBNetsvcName        = "<<image>>zirconb.img"
	ZirconRNetsvcName        = "<<image>>zirconr.img"

	// The GCS library emits this error when it encounters a checksum failure:
	// https://github.com/googleapis/google-cloud-go/blob/07c804b08b9f2bbe181ffcee1b9b41005463b057/storage/reader.go#L415
	BadCRCErrorMsg = "storage: bad CRC on read"
)

func FailedToSendErrMsg(imgName string) string {
	return fmt.Sprintf("failed to send %s", imgName)
}
