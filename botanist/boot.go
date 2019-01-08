// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"sort"
	"time"

	"fuchsia.googlesource.com/tools/netboot"
	"fuchsia.googlesource.com/tools/retry"
	"fuchsia.googlesource.com/tools/tftp"
)

const (
	// ModePave is a directive to pave when booting.
	ModePave int = iota
	// ModeNetboot is a directive to netboot when booting.
	ModeNetboot
)

const (
	// Special image names recognized by fuchsia's netsvc.
	cmdlineNetsvcName = "<<netboot>>cmdline"
	kernelNetsvcName  = "<<netboot>>kernel.bin"
	fvmNetsvcName     = "<<image>>sparse.fvm"
	efiNetsvcName     = "<<image>>efi.img"
	kerncNetsvcName   = "<<image>>kernc.img"
	zirconANetsvcName = "<<image>>zircona.img"
	zirconBNetsvcName = "<<image>>zirconb.img"
	zirconRNetsvcName = "<<image>>zirconr.img"
	sshNetsvcName     = "<<image>>authorized_keys"
)

// The order in which bootserver serves images to netsvc.
// Must be a subsequence of the order given in
// https://fuchsia.googlesource.com/zircon/+/master/system/host/bootserver/bootserver.c
var bootserverImgOrder = map[string]int{
	"storage-sparse": 1,
	"efi":            2,
	"zircon-vboot":   3,
	"zircon-a":       4,
	"zircon-b":       5,
	"zircon-r":       6,
}

// GetNetsvcName returns the associated name recognized by fuchsia's netsvc.
func GetNetsvcName(img Image) (string, bool) {
	m := map[string]string{
		"storage-sparse": fvmNetsvcName,
		"efi":            efiNetsvcName,
		"zircon-a":       zirconANetsvcName,
		"zircon-b":       zirconBNetsvcName,
		"zircon-r":       zirconRNetsvcName,
		"zircon-vboot":   kerncNetsvcName,
	}
	name, ok := m[img.Name]
	return name, ok
}

// Returns whether the list of strings containst "--boot". If an image has such a switch
// in its bootserver args, it should be treated as a kernel image.
func containsBootSwitch(strs []string) bool {
	for _, s := range strs {
		if s == "--boot" {
			return true
		}
	}
	return false
}

// Boot prepares and boots a device at the given IP address. Depending on bootMode, the
// device will either be paved or netbooted with the provided images, command-line
// arguments and a public SSH user key.
func Boot(ctx context.Context, addr *net.UDPAddr, bootMode int, imgs []Image, cmdlineArgs []string, sshKey []byte) error {
	var ramKernel *Image
	var paveImgs []Image
	if bootMode == ModePave {
		// Key on whether bootserver paving args are present to determine if the image is used
		// to pave.
		for i, _ := range imgs {
			if len(imgs[i].PaveArgs) > 0 {
				if containsBootSwitch(imgs[i].PaveArgs) {
					ramKernel = &imgs[i]
				}
				paveImgs = append(paveImgs, imgs[i])
			}
		}
		sort.Slice(paveImgs, func(i, j int) bool {
			return bootserverImgOrder[paveImgs[i].Name] < bootserverImgOrder[paveImgs[j].Name]
		})
	} else if bootMode == ModeNetboot {
		ramKernel = GetImage(imgs, "netboot")
	} else {
		return fmt.Errorf("invalid boot mode: %d", bootMode)
	}

	if err := transfer(ctx, addr, paveImgs, ramKernel, cmdlineArgs, sshKey); err != nil {
		return err
	}

	// If we do not load a kernel into RAM, then we reboot back into the first kernel
	// partition; else we boot directly from RAM.
	// TODO(ZX-2069): Eventually, no ramKernel should be present.
	n := netboot.NewClient(time.Second)
	if ramKernel == nil {
		return n.Reboot(addr)
	} else {
		return n.Boot(addr)
	}
}

// A struct represenitng a file to send per the netboot protocol.
type netsvcFile struct {
	reader io.Reader
	size   int64
	path   string
	name   string
}

func (f netsvcFile) close() error {
	closer, ok := (f.reader).(io.Closer)
	if ok {
		return closer.Close()
	}
	return nil
}

func openNetsvcFile(path, name string) (*netsvcFile, error) {
	fd, err := os.Open(path)
	if err != nil {
		fd.Close()
		return nil, err
	}
	fi, err := fd.Stat()
	if err != nil {
		fd.Close()
		return nil, err
	}
	return &netsvcFile{reader: fd, size: fi.Size(), name: name}, nil
}

// Transfers images with the appropriate netboot prefixes over TFTP to a node at a given
// address.
func transfer(ctx context.Context, addr *net.UDPAddr, imgs []Image, ramKernel *Image, cmdlineArgs []string, sshKey []byte) error {
	// Prepare all files to be tranferred, minding the order, which follows that of the
	// bootserver host tool.
	netsvcFiles := []*netsvcFile{}

	if len(cmdlineArgs) > 0 {
		var buf bytes.Buffer
		for _, arg := range cmdlineArgs {
			fmt.Fprintf(&buf, "%s\n", arg)
		}
		cmdlineNetsvcFile := &netsvcFile{
			reader: bytes.NewReader(buf.Bytes()),
			size:   int64(buf.Len()),
			name:   cmdlineNetsvcName,
		}
		netsvcFiles = append(netsvcFiles, cmdlineNetsvcFile)
	}

	for _, img := range imgs {
		netsvcName, ok := GetNetsvcName(img)
		if !ok {
			return fmt.Errorf("Could not find associated netsvc name for %s", img.Name)
		}
		imgNetsvcFile, err := openNetsvcFile(img.Path, netsvcName)
		if err != nil {
			return err
		}
		defer imgNetsvcFile.close()
		netsvcFiles = append(netsvcFiles, imgNetsvcFile)
	}

	if len(sshKey) > 0 {
		sshNetsvcFile := &netsvcFile{
			reader: bytes.NewReader(sshKey),
			size: int64(len(sshKey)),
			name: sshNetsvcName,
		}
		netsvcFiles = append(netsvcFiles, sshNetsvcFile)
	}

	if ramKernel != nil {
		kernelNetsvcFile, err := openNetsvcFile(ramKernel.Path, kernelNetsvcName)
		if err != nil {
			return err
		}
		defer kernelNetsvcFile.close()
		netsvcFiles = append(netsvcFiles, kernelNetsvcFile)
	}

	t := tftp.NewClient()
	tftpAddr := &net.UDPAddr{
		IP:   addr.IP,
		Port: tftp.ClientPort,
		Zone: addr.Zone,
	}

	// Attempt the whole process of sending every file over and retry on failure of any file.
	// This behavior more closely aligns with that of the bootserver.
	return retry.Retry(ctx, retry.WithMaxRetries(retry.NewConstantBackoff(time.Second), 10), func() error {
		for _, f := range netsvcFiles {
			select {
			case <-ctx.Done():
				return nil
			default:
			}
			// Attempt to send a file. If the server tells us we need to wait, then try
			// again as long as it keeps telling us this. ErrShouldWait implies the server
			// is still responding and will eventually be able to handle our request.
			for {
				log.Printf("attempting to send %s...\n", f.name)
				reader, err := t.Send(tftpAddr, f.name, f.size)
				switch {
				case err == tftp.ErrShouldWait:
					// The target is busy, so let's sleep for a bit before
					// trying again, otherwise we'll be wasting cycles and
					// printing too often.
					log.Printf("target is busy, retrying in one second\n")
					select {
					case <-time.After(time.Second):
						continue
					}
				case err != nil:
					log.Printf("failed to send %s; starting from the top: %v\n", f.name, err)
					return err
				}
				if _, err := reader.ReadFrom(f.reader); err != nil {
					log.Printf("unable to read from %s; retrying: %v\n", f.name, err)
					return err
				}
				break
			}
			log.Printf("done\n")
		}
		return nil
	}, nil)
}
