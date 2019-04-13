// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"sort"
	"time"

	"fuchsia.googlesource.com/tools/build"
	"fuchsia.googlesource.com/tools/netboot"
	"fuchsia.googlesource.com/tools/retry"
	"fuchsia.googlesource.com/tools/tftp"
	"golang.org/x/crypto/ssh"
)

const (
	// ModePave is a directive to pave when booting.
	ModePave int = iota
	// ModeNetboot is a directive to netboot when booting.
	ModeNetboot
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
	"--zircona":    zirconANetsvcName,
	"--zirconb":    zirconBNetsvcName,
	"--zirconr":    zirconRNetsvcName,
}

// Maps netsvc name to the index at which the corresponding file should be transferred if
// present. The indices correspond to the ordering given in
// https://fuchsia.googlesource.com/zircon/+/master/system/host/bootserver/bootserver.c
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
	authorizedKeysNetsvcName: 11,
	kernelNetsvcName:         12,
}

// Boot prepares and boots a device at the given IP address. Depending on bootMode, the
// device will either be paved or netbooted with the provided images, command-line
// arguments and a public SSH user key.
func Boot(ctx context.Context, addr *net.UDPAddr, bootMode int, imgs []build.Image, cmdlineArgs []string, signers []ssh.Signer) error {
	var bootArgs func(build.Image) []string
	switch bootMode {
	case ModePave:
		bootArgs = func(img build.Image) []string { return img.PaveArgs }
	case ModeNetboot:
		bootArgs = func(img build.Image) []string { return img.NetbootArgs }
	default:
		return fmt.Errorf("invalid boot mode: %d", bootMode)
	}

	var files []*netsvcFile
	if len(cmdlineArgs) > 0 {
		var buf bytes.Buffer
		for _, arg := range cmdlineArgs {
			fmt.Fprintf(&buf, "%s\n", arg)
		}
		cmdlineFile, err := newNetsvcFile(cmdlineNetsvcName, buf.Bytes())
		if err != nil {
			return err
		}
		files = append(files, cmdlineFile)
	}

	for _, img := range imgs {
		for _, arg := range bootArgs(img) {
			name, ok := bootserverArgToName[arg]
			if !ok {
				return fmt.Errorf("unrecognized bootserver argument found: %s", arg)
			}
			imgFile, err := openNetsvcFile(name, img.Path)
			if err != nil {
				return err
			}
			defer imgFile.close()
			files = append(files, imgFile)
		}
	}

	if bootMode == ModePave && len(signers) > 0 {
		var authorizedKeys []byte
		for _, s := range signers {
			authorizedKey := ssh.MarshalAuthorizedKey(s.PublicKey())
			authorizedKeys = append(authorizedKeys, authorizedKey...)
		}
		authorizedKeysFile, err := newNetsvcFile(authorizedKeysNetsvcName, authorizedKeys)
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
	if err := transfer(ctx, addr, files); err != nil {
		return err
	}

	// If we do not load a kernel into RAM, then we reboot back into the first kernel
	// partition; else we boot directly from RAM.
	// TODO(ZX-2069): Eventually, no such kernel should be present.
	hasRAMKernel := files[len(files)-1].name == kernelNetsvcName
	n := netboot.NewClient(time.Second)
	if hasRAMKernel {
		return n.Boot(addr)
	}
	return n.Reboot(addr)
}

// A file to send to netsvc.
type netsvcFile struct {
	name   string
	reader io.Reader
	size   int64
	index  int
}

func (f netsvcFile) close() error {
	closer, ok := (f.reader).(io.Closer)
	if ok {
		return closer.Close()
	}
	return nil
}

func openNetsvcFile(name, path string) (*netsvcFile, error) {
	idx, ok := transferOrder[name]
	if !ok {
		return nil, fmt.Errorf("unrecognized name: %s", name)
	}
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
	return &netsvcFile{name: name, reader: fd, size: fi.Size(), index: idx}, nil
}

func newNetsvcFile(name string, buf []byte) (*netsvcFile, error) {
	idx, ok := transferOrder[name]
	if !ok {
		return nil, fmt.Errorf("unrecognized name: %s", name)
	}
	return &netsvcFile{
		reader: bytes.NewReader(buf),
		size:   int64(len(buf)),
		name:   name,
		index:  idx,
	}, nil
}

// Transfers files over TFTP to a node at a given address.
func transfer(ctx context.Context, addr *net.UDPAddr, files []*netsvcFile) error {
	t := tftp.NewClient()
	tftpAddr := &net.UDPAddr{
		IP:   addr.IP,
		Port: tftp.ClientPort,
		Zone: addr.Zone,
	}

	// Attempt the whole process of sending every file over and retry on failure of any file.
	// This behavior more closely aligns with that of the bootserver.
	return retry.Retry(ctx, retry.WithMaxRetries(retry.NewConstantBackoff(time.Second), 20), func() error {
		for _, f := range files {
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
