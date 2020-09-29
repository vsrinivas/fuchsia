// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
	"go.fuchsia.dev/fuchsia/tools/net/netutil"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
)

const (
	bootloaderVersion             = "0.7.22"
	defaultTftpBlockSize          = 1428
	defaultTftpWindowSize         = 256
	defaultMicrosecBetweenPackets = 20
	nodenameEnvKey                = "ZIRCON_NODENAME"
	retryDelay                    = time.Second
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
	authorizedKeysFile             string
	failFast                       bool
	useNetboot                     bool
	useTftp                        bool
	nocolor                        bool
	allowZedbootVersionMismatch    bool
	failFastZedbootVersionMismatch bool
	imageManifest                  string
	mode                           bootserver.Mode

	// errIncompleteTransfer represents a failure to transfer pave images
	// to the device.
	errIncompleteTransfer = errors.New("transfer incomplete")
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

	flag.BoolVar(&allowZedbootVersionMismatch, "allow-zedboot-version-mismatch", false, "warn on zedboot version mismatch rather than fail")
	flag.StringVar(&authorizedKeysFile, "authorized-keys", "", "use the supplied file as an authorized_keys file")
	flag.StringVar(&boardName, "board_name", "", "name of the board files are meant for")
	flag.BoolVar(&bootOnce, "1", true, "only boot once, then exit")
	flag.BoolVar(&failFast, "fail-fast", false, "exit on first error")
	flag.BoolVar(&failFastZedbootVersionMismatch, "fail-fast-if-version-mismatch", false, "error if zedboot version does not match")

	//  TODO(fxbug.dev/38517): Implement the following unsupported flags.
	flag.StringVar(&bootIpv6, "a", "", "only boot device with this IPv6 address")
	flag.IntVar(&tftpBlockSize, "b", defaultTftpBlockSize, "tftp block size")
	flag.IntVar(&packetInterval, "i", defaultMicrosecBetweenPackets, "number of microseconds between packets; ignored with --tftp")
	flag.IntVar(&windowSize, "w", defaultTftpWindowSize, "tftp window size, ignored with --netboot")
	// We currently always default to tftp
	flag.BoolVar(&useNetboot, "netboot", false, "use the netboot protocol")
	flag.BoolVar(&useTftp, "tftp", true, "use the tftp protocol (default)")
	flag.BoolVar(&nocolor, "nocolor", false, "disable ANSI color (false)")
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

func overrideImage(ctx context.Context, imgMap map[string]bootserver.Image, img bootserver.Image) map[string]bootserver.Image {
	if existing, ok := imgMap[img.Name]; ok {
		if existing.Reader != nil {
			if closer, ok := existing.Reader.(io.Closer); ok {
				if err := closer.Close(); err != nil {
					logger.Warningf(ctx, "failed to close image %s: %v", existing.Name, err)
				}
			}
		}
	}
	imgMap[img.Name] = img
	return imgMap
}

func getImages(ctx context.Context) ([]bootserver.Image, func() error, error) {
	imgMap := make(map[string]bootserver.Image)
	// If an image manifest is provided, we use that.
	if imageManifest != "" {
		imgs, closeFunc, err := bootserver.GetImages(ctx, imageManifest, mode)
		if err != nil {
			return imgs, closeFunc, err
		}
		for _, img := range imgs {
			imgMap[img.Name] = img
		}
	}
	// If cmdline args are provided, append them to the image list, overriding any
	// images from the manifest.
	if bootKernel != "" {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			Name: "zbi_netboot",
			Path: bootKernel,
			Args: []string{"--boot"},
		})
	}
	if bootloader != "" {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			Name: "blk_efi",
			Path: bootloader,
			Args: []string{"--bootloader"},
		})
	}
	for _, fw := range firmware {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			// Trailing delimiter is OK for untyped images.
			Name: "img_firmware_" + fw.fwType,
			Path: fw.value,
			Args: []string{"--firmware-" + fw.fwType},
		})
	}
	if fvm != "" {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			Name: "blk_storage-sparse",
			Path: fvm,
			Args: []string{"--fvm"},
		})
	}
	if zircona != "" {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			Name: "zbi_zircon-a",
			Path: zircona,
			Args: []string{"--zircona"},
		})
	}
	if zirconb != "" {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			Name: "zbi_zircon-b",
			Path: zirconb,
			Args: []string{"--zirconb"},
		})
	}
	if zirconr != "" {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			Name: "zbi_zircon-r",
			Path: zirconr,
			Args: []string{"--zirconr"},
		})
	}
	if vbmetaa != "" {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			Name: "vbmeta_zircon-a",
			Path: vbmetaa,
			Args: []string{"--vbmetaa"},
		})
	}
	if vbmetab != "" {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			Name: "vbmeta_zircon-b",
			Path: vbmetab,
			Args: []string{"--vbmetab"},
		})
	}
	if vbmetar != "" {
		imgMap = overrideImage(ctx, imgMap, bootserver.Image{
			Name: "vbmeta_zircon-r",
			Path: vbmetar,
			Args: []string{"--vbmetar"},
		})
	}
	imgs := []bootserver.Image{}
	for _, img := range imgMap {
		imgs = append(imgs, img)
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
		if imgs[i].Reader != nil {
			continue
		}
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

func connectAndBoot(ctx context.Context, nodename string, imgs []bootserver.Image, cmdlineArgs []string, authorizedKeys []byte) error {
	addr, msg, conn, err := netutil.GetAdvertisement(ctx, nodename)
	if err != nil {
		return fmt.Errorf("%w: %v", errIncompleteTransfer, err)
	}
	defer conn.Close()
	if msg.BootloaderVersion != bootloaderVersion {
		mismatchErrMsg := fmt.Sprintf("WARNING: Bootserver version '%s' != remote Zedboot version '%s'", bootloaderVersion, msg.BootloaderVersion)
		if allowZedbootVersionMismatch {
			logger.Warningf(ctx, "%s. Paving may fail.", mismatchErrMsg)
		} else {
			if failFastZedbootVersionMismatch {
				failFast = true
			}
			return fmt.Errorf("%w: %s. Device will not be serviced. Please upgrade Zedboot.", errIncompleteTransfer, mismatchErrMsg)
		}
	}
	udpAddr := &net.UDPAddr{
		IP:   addr.IP,
		Port: tftp.ClientPort,
		Zone: addr.Zone,
	}
	client, err := tftp.NewClient(udpAddr)
	if err != nil {
		return fmt.Errorf("%w: %v", errIncompleteTransfer, err)
	}

	if boardName != "" {
		if err := bootserver.ValidateBoard(ctx, client, boardName); err != nil {
			return err
		}
	}

	logger.Infof(ctx, "Proceeding with nodename %s", msg.Nodename)

	if err := bootserver.Boot(ctx, client, imgs, cmdlineArgs, authorizedKeys); err != nil {
		return fmt.Errorf("%w: %v", errIncompleteTransfer, err)
	}
	return nil
}

func resolveNodename() (string, error) {
	if nodename == "" {
		envNodename, ok := os.LookupEnv(nodenameEnvKey)
		if ok && envNodename != "" {
			return envNodename, nil
		} else {
			// Return the NodenameWildcard to discover any device.
			return netboot.NodenameWildcard, nil
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

	if boardName != "" {
		logger.Infof(ctx, "Board name set to [%s]", boardName)
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

	if allowZedbootVersionMismatch && failFastZedbootVersionMismatch {
		return fmt.Errorf("only one of allow-zedboot-version-mismatch and fail-fast-if-version-mismatch can be true")
	}

	var authorizedKeys []byte
	if authorizedKeysFile != "" {
		authorizedKeys, err = ioutil.ReadFile(authorizedKeysFile)
		if err != nil {
			return fmt.Errorf("could not read SSH key file %q: %v", authorizedKeysFile, err)
		}
	}

	// Keep discovering and booting devices.
	for {
		err = connectAndBoot(ctx, n, imgs, cmdlineArgs, authorizedKeys)
		if !(err == nil || errors.Is(err, errIncompleteTransfer)) {
			// Exit early for any error other than an errIncompleteTransfer.
			return err
		} else if bootOnce || (failFast && err != nil) {
			// Exit after booting first device or if boot failed.
			return err
		} else if err != nil {
			// Error is an errIncompleteTransfer. Retry after some delay.
			logger.Errorf(ctx, err.Error())
			time.Sleep(retryDelay)
		}
	}
}

func main() {
	flag.Parse()

	ctx := context.Background()
	if err := execute(ctx, flag.Args()); err != nil {
		logger.Fatalf(ctx, err.Error())
	}
}
