// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/bootserver/lib"
	"go.fuchsia.dev/fuchsia/tools/net/netutil"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
)

const (
	defaultTftpBlockSize          = 1428
	defaultTftpWindowSize         = 256
	defaultMicrosecBetweenPackets = 20
	nodenameEnvVar                = "ZIRCON_NODENAME"
)

// Firmware arguments have variable names based on their type e.g.:
//   --firmware=<file>
//   --firmware-foo=<file>
// The flag package doesn't support anything like this, so we have to do a  bit
// of extra work to find any of these args and add them to the list.
type firmwareArg struct {
	name   string
	value  string
	fwType string
	help   string
}

var (
	bootOnce                       bool
	bootIpv6                       string
	tftpBlockSize                  int
	packetInterval                 int
	nodename                       string
	windowSize                     int
	boardName                      string
	bootKernel                     string
	fvm                            string
	bootloader                     string
	firmware                       []firmwareArg
	zircona                        string
	zirconb                        string
	zirconr                        string
	vbmetaa                        string
	vbmetab                        string
	vbmetar                        string
	authorizedKeys                 string
	failFast                       bool
	useNetboot                     bool
	useTftp                        bool
	nocolor                        bool
	allowZedbootVersionMismatch    bool
	failFastZedbootVersionMismatch bool
	imageManifest                  string
	mode                           bootserver.Mode
)

func init() {
	// Support classic cmd line interface.
	flag.StringVar(&nodename, "n", "", "only boot device with this nodename")
	flag.StringVar(&bootKernel, "boot", "", "use the supplied file as a kernel")
	flag.StringVar(&fvm, "fvm", "", "use the supplied file as a sparse FVM image (up to 4 times)")
	flag.StringVar(&bootloader, "bootloader", "", "use the supplied file as a bootloader image")
	flag.StringVar(&zircona, "zircona", "", "use the supplied file as a zircon-a zbi")
	flag.StringVar(&zirconb, "zirconb", "", "use the supplied file as a zircon-b zbi")
	flag.StringVar(&zirconr, "zirconr", "", "use the supplied file as a zircon-r zbi")
	flag.StringVar(&vbmetaa, "vbmetaa", "", "use the supplied file as a avb vbmeta_a image")
	flag.StringVar(&vbmetab, "vbmetab", "", "use the supplied file as a avb vbmeta_b image")
	flag.StringVar(&vbmetar, "vbmetar", "", "use the supplied file as a avb vbmeta_r image")

	// Support firmware arguments.
	firmware = getFirmwareArgs(os.Args)
	for i := range firmware {
		flag.StringVar(&firmware[i].value, firmware[i].name, "", firmware[i].help)
	}

	// Support reading in images.json and paving zedboot.
	flag.StringVar(&imageManifest, "images", "", "use an image manifest to pave")
	flag.Var(&mode, "mode", "bootserver modes: either pave, netboot, or pave-zedboot")

	//  TODO(fxbug.dev/38517): Implement the following unsupported flags.
	flag.BoolVar(&bootOnce, "1", false, "only boot once, then exit")
	flag.StringVar(&bootIpv6, "a", "", "only boot device with this IPv6 address")
	flag.IntVar(&tftpBlockSize, "b", defaultTftpBlockSize, "tftp block size")
	flag.IntVar(&packetInterval, "i", defaultMicrosecBetweenPackets, "number of microseconds between packets; ignored with --tftp")
	flag.IntVar(&windowSize, "w", defaultTftpWindowSize, "tftp window size, ignored with --netboot")
	flag.StringVar(&boardName, "board_name", "", "name of the board files are meant for")
	flag.StringVar(&authorizedKeys, "authorized-keys", "", "use the supplied file as an authorized_keys file")
	flag.BoolVar(&failFast, "fail-fast", false, "exit on first error")
	// We currently always default to tftp
	flag.BoolVar(&useNetboot, "netboot", false, "use the netboot protocol")
	flag.BoolVar(&useTftp, "tftp", true, "use the tftp protocol (default)")
	flag.BoolVar(&nocolor, "nocolor", false, "disable ANSI color (false)")
	flag.BoolVar(&allowZedbootVersionMismatch, "allow-zedboot-version-mismatch", false, "warn on zedboot version mismatch rather than fail")
	flag.BoolVar(&failFastZedbootVersionMismatch, "fail-fast-if-version-mismatch", false, "error if zedboot version does not match")
}

// Creates a slice of FirmwareArgs from |args|.
func getFirmwareArgs(args []string) []firmwareArg {
	// Always include the default --firmware whether specified or not, to give
	// a useful help message.
	fwArgs := []firmwareArg{{
		name:   "firmware",
		value:  "",
		fwType: "",
		help:   "use the supplied file as the default firmware image; use --firmware-<type> for typed images",
	}}

	for _, arg := range args {
		// Go supports one or two dash prefixes.
		fwType := strings.TrimPrefix(arg, "-firmware")
		fwType = strings.TrimPrefix(fwType, "--firmware")
		if fwType == arg {
			// Didn't find a prefix match, on to the next arg.
			continue
		}

		// If the caller used "arg=value", peel off the "=value" part as we're
		// not actually parsing, but just finding all the necessary arg names.
		fwType = strings.SplitN(fwType, "=", 2)[0]

		if fwType == "" {
			// We already added the default untyped "--firmware" arg.
		} else if fwType[0] == '-' {
			fwType = fwType[1:]
			fwArgs = append(fwArgs, firmwareArg{
				name:   fmt.Sprintf("firmware-%s", fwType),
				value:  "",
				fwType: fwType,
				help:   fmt.Sprintf("use the supplied file as the %q firmware image", fwType),
			})
		}

		// If we got here it wasn't a firmware arg, just ignore it.
	}

	return fwArgs
}

func getImages(ctx context.Context) ([]bootserver.Image, func() error, error) {
	// If an image manifest is provided, we use that.
	if imageManifest != "" {
		return bootserver.GetImages(ctx, imageManifest, mode)
	}
	// Otherwise, build an image list from the cmd line args.
	var imgs []bootserver.Image
	if bootKernel != "" {
		imgs = append(imgs, bootserver.Image{
			Name: "zbi_netboot",
			Path: bootKernel,
			Args: []string{"--boot"},
		})
	}
	if bootloader != "" {
		imgs = append(imgs, bootserver.Image{
			Name: "blk_efi",
			Path: bootloader,
			Args: []string{"--bootloader"},
		})
	}
	if fvm != "" {
		imgs = append(imgs, bootserver.Image{
			Name: "blk_storage-sparse",
			Path: fvm,
			Args: []string{"--fvm"},
		})
	}
	if zircona != "" {
		imgs = append(imgs, bootserver.Image{
			Name: "zbi_zircon-a",
			Path: zircona,
			Args: []string{"--zircona"},
		})
	}
	if zirconb != "" {
		imgs = append(imgs, bootserver.Image{
			Name: "zbi_zircon-b",
			Path: zirconb,
			Args: []string{"--zirconb"},
		})
	}
	if zirconr != "" {
		imgs = append(imgs, bootserver.Image{
			Name: "zbi_zircon-r",
			Path: zirconr,
			Args: []string{"--zirconr"},
		})
	}
	if vbmetaa != "" {
		imgs = append(imgs, bootserver.Image{
			Name: "vbmeta_zircon-a",
			Path: vbmetaa,
			Args: []string{"--vbmetaa"},
		})
	}
	if vbmetab != "" {
		imgs = append(imgs, bootserver.Image{
			Name: "vbmeta_zircon-b",
			Path: vbmetab,
			Args: []string{"--vbmetab"},
		})
	}
	if vbmetar != "" {
		imgs = append(imgs, bootserver.Image{
			Name: "vbmeta_zircon-r",
			Path: vbmetar,
			Args: []string{"--vbmetar"},
		})
	}
	closeFunc, err := populateReaders(imgs)
	return imgs, closeFunc, err
}

func populateReaders(imgs []bootserver.Image) (func() error, error) {
	closeFunc := func() error {
		var errs []error
		for _, img := range imgs {
			if img.Reader != nil {
				if closer, ok := img.Reader.(io.Closer); ok {
					if err := closer.Close(); err != nil {
						errs = append(errs, err)
					}
				}
			}
		}
		if len(errs) > 0 {
			return fmt.Errorf("failed to close images: %v", errs)
		}
		return nil
	}
	for i := range imgs {
		r, err := os.Open(imgs[i].Path)
		if err != nil {
			// Close already opened readers.
			closeFunc()
			return closeFunc, fmt.Errorf("failed to open %s: %w", imgs[i].Path, err)
		}
		fi, err := r.Stat()
		if err != nil {
			closeFunc()
			return closeFunc, fmt.Errorf("failed to get file info for %s: %w", imgs[i].Path, err)
		}
		imgs[i].Reader = r
		imgs[i].Size = fi.Size()
	}
	return closeFunc, nil
}

func connectAndBoot(ctx context.Context, nodename string, imgs []bootserver.Image, cmdlineArgs []string) error {
	addr, err := netutil.GetNodeAddress(ctx, nodename, false)
	if err != nil {
		return err
	}
	udpAddr := &net.UDPAddr{
		IP:   addr.IP,
		Port: tftp.ClientPort,
		Zone: addr.Zone,
	}
	client, err := tftp.NewClient(udpAddr)
	if err != nil {
		return err
	}

	// TODO(fxbug.dev/38517): Create ssh signers if an authorized key file was provided along with the image manifest.
	return bootserver.Boot(ctx, client, imgs, cmdlineArgs, nil)
}

func resolveNodename() (string, error) {
	if nodename == "" {
		envNodename, ok := os.LookupEnv(nodenameEnvVar)
		if ok && envNodename != "" {
			return envNodename, nil
		} else {
			return "", fmt.Errorf("unimplemented: must supply nodename")
		}
	}
	return nodename, nil
}

func execute(ctx context.Context, cmdlineArgs []string) error {
	// Do some secondary cmdline arg validation.
	if imageManifest != "" && mode == bootserver.ModeNull {
		return fmt.Errorf("must specify a bootserver mode [--mode] when using an image manifest")
	} else if imageManifest == "" && mode != bootserver.ModeNull {
		return fmt.Errorf("cannot specify a bootserver mode without an image manifest [--images]")
	}

	// Remove the default firmware if the caller didn't actually use it.
	if firmware[0].value == "" {
		firmware = firmware[1:]
	}

	imgs, closeFunc, err := getImages(ctx)
	if err != nil {
		return err
	}
	if len(imgs) == 0 {
		return fmt.Errorf("no images provided!")
	}
	defer closeFunc()

	n, err := resolveNodename()
	if err != nil {
		return err
	}
	return connectAndBoot(ctx, n, imgs, cmdlineArgs)
}

func main() {
	flag.Parse()

	if err := execute(context.Background(), flag.Args()); err != nil {
		log.Fatal(err)
	}
}
