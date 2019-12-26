// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserver

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"sort"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
	"golang.org/x/crypto/ssh"
)

const (
	// Special image names recognized by fuchsia's netsvc.
	authorizedKeysNetsvcName = "<<image>>authorized_keys"
	bootloaderNetsvcName     = "<<image>>bootloader.img"
	cmdlineNetsvcName        = "<<netboot>>cmdline"
	efiNetsvcName            = "<<image>>efi.img"
	fvmNetsvcName            = "<<image>>sparse.fvm"
	kerncNetsvcName          = "<<image>>kernc.img"
	kernelNetsvcName         = "<<netboot>>kernel.bin"
	vbmetaANetsvcName        = "<<image>>vbmetaa.img"
	vbmetaBNetsvcName        = "<<image>>vbmetab.img"
	vbmetaRNetsvcName        = "<<image>>vbmetar.img"
	zirconANetsvcName        = "<<image>>zircona.img"
	zirconBNetsvcName        = "<<image>>zirconb.img"
	zirconRNetsvcName        = "<<image>>zirconr.img"
)

// Maps bootserver argument to a corresponding netsvc name.
var bootserverArgToName = map[string]string{
	"--boot":       kernelNetsvcName,
	"--bootloader": bootloaderNetsvcName,
	"--efi":        efiNetsvcName,
	"--fvm":        fvmNetsvcName,
	"--kernc":      kerncNetsvcName,
	"--vbmetaa":    vbmetaANetsvcName,
	"--vbmetab":    vbmetaBNetsvcName,
	"--vbmetar":    vbmetaRNetsvcName,
	"--zircona":    zirconANetsvcName,
	"--zirconb":    zirconBNetsvcName,
	"--zirconr":    zirconRNetsvcName,
}

// Maps netsvc name to the index at which the corresponding file should be transferred if
// present. The indices correspond to the ordering given in
// https://go.fuchsia.dev/zircon/+/master/system/host/bootserver/bootserver.c
var transferOrder = map[string]int{
	cmdlineNetsvcName:        1,
	fvmNetsvcName:            2,
	bootloaderNetsvcName:     3,
	efiNetsvcName:            4,
	kerncNetsvcName:          5,
	zirconANetsvcName:        6,
	zirconBNetsvcName:        7,
	zirconRNetsvcName:        8,
	vbmetaANetsvcName:        9,
	vbmetaBNetsvcName:        10,
	vbmetaRNetsvcName:        11,
	authorizedKeysNetsvcName: 12,
	kernelNetsvcName:         13,
}

// Boot prepares and boots a device at the given IP address.
func Boot(ctx context.Context, t tftp.Client, imgs []Image, cmdlineArgs []string, signers []ssh.Signer) error {
	var files []*netsvcFile
	if len(cmdlineArgs) > 0 {
		var buf bytes.Buffer
		for _, arg := range cmdlineArgs {
			fmt.Fprintf(&buf, "%s\n", arg)
		}
		reader := bytes.NewReader(buf.Bytes())
		cmdlineFile, err := newNetsvcFile(cmdlineNetsvcName, reader, reader.Size())
		if err != nil {
			return err
		}
		files = append(files, cmdlineFile)
	}

	for _, img := range imgs {
		for _, arg := range img.Args {
			name, ok := bootserverArgToName[arg]
			if !ok {
				return fmt.Errorf("unrecognized bootserver argument found: %s", arg)
			}
			imgFile, err := newNetsvcFile(name, img.Reader, img.Size)
			if err != nil {
				return err
			}
			files = append(files, imgFile)
		}
	}

	// Convert the authorized keys into a netsvc file.
	if len(signers) > 0 {
		var authorizedKeys []byte
		for _, s := range signers {
			authorizedKey := ssh.MarshalAuthorizedKey(s.PublicKey())
			authorizedKeys = append(authorizedKeys, authorizedKey...)
		}
		reader := bytes.NewReader(authorizedKeys)
		authorizedKeysFile, err := newNetsvcFile(authorizedKeysNetsvcName, reader, reader.Size())
		if err != nil {
			return err
		}
		files = append(files, authorizedKeysFile)
	}

	sort.Slice(files, func(i, j int) bool {
		return files[i].index < files[j].index
	})

	if len(files) == 0 {
		return errors.New("no files to transfer")
	}
	if err := transfer(ctx, t, files); err != nil {
		return err
	}

	// If we do not load a kernel into RAM, then we reboot back into the first kernel
	// partition; else we boot directly from RAM.
	// TODO(ZX-2069): Eventually, no such kernel should be present.
	hasRAMKernel := files[len(files)-1].name == kernelNetsvcName
	n := netboot.NewClient(time.Second)
	if hasRAMKernel {
		// Try to send the boot command a few times, as there's no ack, so it's
		// not possible to tell if it's successfully booted or not.
		for i := 0; i < 5; i++ {
			n.Boot(t.RemoteAddr())
		}
	}
	return n.Reboot(t.RemoteAddr())
}

// BootZedbootShim extracts the Zircon-R image that is intended to be paved to the device
// and mexec()'s it, it is intended to be executed before calling Boot().
// This function serves to emulate zero-state, and will eventually be superseded by an
// infra implementation.
func BootZedbootShim(ctx context.Context, t tftp.Client, imgs []Image) error {
	files, err := filterZedbootShimImages(imgs)
	if err != nil {
		return err
	}

	sort.Slice(files, func(i, j int) bool {
		return files[i].index < files[j].index
	})

	if err := transfer(ctx, t, files); err != nil {
		return err
	}
	hasRAMKernel := files[len(files)-1].name == kernelNetsvcName
	n := netboot.NewClient(time.Second)
	if hasRAMKernel {
		return n.Boot(t.RemoteAddr())
	}
	return n.Reboot(t.RemoteAddr())
}

func filterZedbootShimImages(imgs []Image) ([]*netsvcFile, error) {
	netsvcName := kernelNetsvcName
	bootloaderImg := Image{}
	vbmetaRImg := Image{}
	zirconRImg := Image{}
	for _, img := range imgs {
		for _, arg := range img.Args {
			// Find name by bootserver arg to ensure we are extracting the correct zircon-r.
			// There may be more than one in images.json but only one should be passed to
			// the bootserver for paving.
			name, ok := bootserverArgToName[arg]
			if !ok {
				return nil, fmt.Errorf("unrecognized bootserver argument found: %q", arg)
			}
			switch name {
			case bootloaderNetsvcName:
				bootloaderImg = img
			case vbmetaRNetsvcName:
				vbmetaRImg = img
			case zirconRNetsvcName:
				zirconRImg = img
				// Signed ZBIs cannot be mexec()'d, so pave them to A and boot instead.
				if strings.HasSuffix(img.Name, ".signed") {
					netsvcName = zirconANetsvcName
				}
			default:
				continue
			}
		}
	}

	if zirconRImg.Reader != nil {
		files := []*netsvcFile{}
		zedbootFile, err := newNetsvcFile(netsvcName, zirconRImg.Reader, zirconRImg.Size)
		if err != nil {
			return nil, err
		}
		files = append(files, zedbootFile)
		if vbmetaRImg.Reader != nil && netsvcName == zirconANetsvcName {
			vbmetaRFile, err := newNetsvcFile(vbmetaANetsvcName, vbmetaRImg.Reader, vbmetaRImg.Size)
			if err != nil {
				return nil, err
			}
			files = append(files, vbmetaRFile)
		}
		if bootloaderImg.Reader != nil {
			bootloaderFile, err := newNetsvcFile(bootloaderNetsvcName, bootloaderImg.Reader, bootloaderImg.Size)
			if err != nil {
				return nil, err
			}
			files = append(files, bootloaderFile)
		}
		return files, nil
	}

	return nil, fmt.Errorf("no zircon-r image found in: %v", imgs)
}

// A file to send to netsvc.
type netsvcFile struct {
	name   string
	reader io.ReaderAt
	index  int
	size   int64
}

func newNetsvcFile(name string, reader io.ReaderAt, size int64) (*netsvcFile, error) {
	idx, ok := transferOrder[name]
	if !ok {
		return nil, fmt.Errorf("unrecognized name: %s", name)
	}
	return &netsvcFile{
		reader: reader,
		name:   name,
		index:  idx,
		size:   size,
	}, nil
}

// Transfers files over TFTP to a node at a given address.
func transfer(ctx context.Context, t tftp.Client, files []*netsvcFile) error {
	// Attempt the whole process of sending every file over and retry on failure of any file.
	// This behavior more closely aligns with that of the bootserver.
	return retry.Retry(ctx, retry.WithMaxRetries(retry.NewConstantBackoff(time.Second), 20), func() error {
		for _, f := range files {
			// Attempt to send a file. If the server tells us we need to wait, then try
			// again as long as it keeps telling us this. ErrShouldWait implies the server
			// is still responding and will eventually be able to handle our request.
			log.Printf("attempting to send %s...\n", f.name)
			for {
				if ctx.Err() != nil {
					return nil
				}
				err := t.Write(ctx, f.name, f.reader, f.size)
				switch err {
				case nil:
				case tftp.ErrShouldWait:
					// The target is busy, so let's sleep for a bit before
					// trying again, otherwise we'll be wasting cycles and
					// printing too often.
					log.Printf("target is busy, retrying in one second\n")
					time.Sleep(time.Second)
					continue
				default:
					log.Printf("failed to send %s; starting from the top: %v\n", f.name, err)
					return err
				}
				break
			}
			log.Printf("done\n")
		}
		return nil
	}, nil)
}
