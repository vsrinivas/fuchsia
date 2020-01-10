// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	zxio "syscall/zx/io"

	"app/context"
	fuchsiaio "fidl/fuchsia/io"
	"fidl/fuchsia/mem"
	"fidl/fuchsia/paver"
	"fidl/fuchsia/pkg"
	"syslog"
)

type Package struct {
	namever string
	merkle  string
}

func ConnectToPackageResolver() (*pkg.PackageResolverInterface, error) {
	context := context.CreateFromStartupInfo()
	req, pxy, err := pkg.NewPackageResolverInterfaceRequest()

	if err != nil {
		syslog.Errorf("control interface could not be acquired: %s", err)
		return nil, err
	}

	context.ConnectToEnvService(req)
	return pxy, nil
}

func ConnectToPaver() (*paver.DataSinkInterface, *paver.BootManagerInterface, error) {
	context := context.CreateFromStartupInfo()
	req, pxy, err := paver.NewPaverInterfaceRequest()

	if err != nil {
		syslog.Errorf("control interface could not be acquired: %s", err)
		return nil, nil, err
	}
	defer pxy.Close()

	context.ConnectToEnvService(req)

	dataSinkReq, dataSinkPxy, err := paver.NewDataSinkInterfaceRequest()
	if err != nil {
		syslog.Errorf("data sink interface could not be acquired: %s", err)
		return nil, nil, err
	}

	err = pxy.FindDataSink(dataSinkReq)
	if err != nil {
		syslog.Errorf("could not find data sink: %s", err)
		return nil, nil, err
	}

	bootManagerReq, bootManagerPxy, err := paver.NewBootManagerInterfaceRequest()
	if err != nil {
		syslog.Errorf("boot manager interface could not be acquired: %s", err)
		return nil, nil, err
	}

	err = pxy.FindBootManager(bootManagerReq, false)
	if err != nil {
		syslog.Errorf("could not find boot manager: %s", err)
		return nil, nil, err
	}

	return dataSinkPxy, bootManagerPxy, nil
}

// CacheUpdatePackage caches the requested, possibly merkle-pinned, update
// package URL and returns the pkgfs path to the package.
func CacheUpdatePackage(updateURL string, resolver *pkg.PackageResolverInterface) (string, error) {
	dirPxy, err := resolvePackage(updateURL, resolver)
	if err != nil {
		return "", err
	}
	defer dirPxy.Close()

	channelProxy := (*fidl.ChannelProxy)(dirPxy)
	updateDir := fdio.Directory{fdio.Node{(*zxio.NodeInterface)(channelProxy)}}
	path := "meta"
	f, err := updateDir.Open(path, zxio.OpenRightReadable, zxio.ModeTypeFile)
	if err != nil {
		return "", err
	}
	file := os.NewFile(uintptr(syscall.OpenFDIO(f)), path)
	defer file.Close()
	b, err := ioutil.ReadAll(file)
	if err != nil {
		return "", err
	}
	merkle := string(b)
	return "/pkgfs/versions/" + merkle, nil
}

func ParseRequirements(pkgSrc io.ReadCloser, imgSrc io.ReadCloser) ([]*Package, []string, error) {
	imgs := []string{}
	pkgs := []*Package{}

	rdr := bufio.NewReader(pkgSrc)
	for {
		l, err := rdr.ReadString('\n')
		s := strings.TrimSpace(l)
		if (err == nil || err == io.EOF) && len(s) > 0 {
			entry := strings.Split(s, "=")
			if len(entry) != 2 {
				return nil, nil, fmt.Errorf("parser: entry format %q", s)
			} else {
				pkgs = append(pkgs, &Package{namever: entry[0], merkle: entry[1]})
			}
		}

		if err != nil {
			if err != io.EOF {
				return nil, nil, fmt.Errorf("parser: got error reading packages file %s", err)
			}
			break
		}
	}

	rdr = bufio.NewReader(imgSrc)

	for {
		l, err := rdr.ReadString('\n')
		s := strings.TrimSpace(l)
		if (err == nil || err == io.EOF) && len(s) > 0 {
			imgs = append(imgs, s)
		}

		if err != nil {
			if err != io.EOF {
				return nil, nil, fmt.Errorf("parser: got error reading images file %s", err)
			}
			break
		}
	}

	return pkgs, imgs, nil
}

func FetchPackages(pkgs []*Package, resolver *pkg.PackageResolverInterface) error {
	var errCount int
	for _, pkg := range pkgs {
		if err := fetchPackage(pkg, resolver); err != nil {
			syslog.Errorf("fetch error: %s", err)
			errCount++
		}
	}

	if errCount > 0 {
		return fmt.Errorf("system update failed, %d packages had errors", errCount)
	}

	return nil
}

func fetchPackage(p *Package, resolver *pkg.PackageResolverInterface) error {
	b, err := ioutil.ReadFile(filepath.Join("/pkgfs/versions", p.merkle, "meta"))
	if err == nil {
		// package is already installed, skip
		if string(b) == p.merkle {
			return nil
		}
	}

	pkgURL := fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s?hash=%s", p.namever, p.merkle)
	dirPxy, err := resolvePackage(pkgURL, resolver)
	if dirPxy != nil {
		dirPxy.Close()
	}
	return err
}

func resolvePackage(pkgURL string, resolver *pkg.PackageResolverInterface) (*fuchsiaio.DirectoryInterface, error) {
	selectors := []string{}
	updatePolicy := pkg.UpdatePolicy{}
	dirReq, dirPxy, err := fuchsiaio.NewDirectoryInterfaceRequest()
	if err != nil {
		return nil, err
	}

	syslog.Infof("requesting %s from update system", pkgURL)

	status, err := resolver.Resolve(pkgURL, selectors, updatePolicy, dirReq)
	if err != nil {
		dirPxy.Close()
		return nil, fmt.Errorf("fetch: Resolve error: %s", err)
	}

	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		dirPxy.Close()
		return nil, fmt.Errorf("fetch: Resolve status: %s", statusErr)
	}

	return dirPxy, nil
}

func ValidateImgs(imgs []string, imgsPath string) error {
	boardPath := filepath.Join(imgsPath, "board")
	actual, err := ioutil.ReadFile(boardPath)
	if err == nil {
		expected, err := ioutil.ReadFile("/config/build-info/board")
		if err != nil {
			return err
		}
		if !bytes.Equal(actual, expected) {
			return fmt.Errorf("parser: expected board name %s found %s", expected, actual)
		}
	} else if !os.IsNotExist(err) {
		return err
	}

	// Require a 'zbi' or 'zbi.signed' partition in the update package.
	found := false
	for _, img := range []string{"zbi", "zbi.signed"} {
		if _, err := os.Stat(filepath.Join(imgsPath, img)); err == nil {
			found = true
			break
		}
	}

	if !found {
		return fmt.Errorf("parser: missing 'zbi' or 'zbi.signed'")
	}

	return nil
}

func WriteImgs(dataSink *paver.DataSinkInterface, bootManager *paver.BootManagerInterface, imgs []string, imgsPath string) error {
	syslog.Infof("Writing images %+v from %q", imgs, imgsPath)

	activeConfig, err := queryActiveConfig(bootManager)
	if err != nil {
		return fmt.Errorf("querying target config: %v", err)
	}

	// If we have an active config (and thus support ABR), compute the
	// target config. Otherwise set the target config to nil so we fall
	// back to the legacy behavior where we write to the A partition, and
	// attempt to write to the B partition.
	var targetConfig *paver.Configuration
	if activeConfig == nil {
		targetConfig = nil
	} else {
		targetConfig, err = calculateTargetConfig(*activeConfig)
		if err != nil {
			return err
		}
	}

	for _, img := range imgs {
		if err := writeImg(dataSink, img, imgsPath, targetConfig); err != nil {
			return err
		}
	}

	if targetConfig != nil {
		if err := setConfigurationActive(bootManager, *targetConfig); err != nil {
			return err
		}
	}

	return nil
}

// queryActiveConfig asks the boot manager what partition the device booted
// from. If the device does not support ABR, it returns nil as the
// configuration.
func queryActiveConfig(bootManager *paver.BootManagerInterface) (*paver.Configuration, error) {
	activeConfig, err := bootManager.QueryActiveConfiguration()
	if err != nil {
		// FIXME(fxb/43577): If the paver service runs into a problem
		// creating a boot manager, it will close the channel with an
		// epitaph. The error we are particularly interested in is
		// whether or not the current device supports ABR.
		// Unfortunately the go fidl bindings do not support epitaphs,
		// so we can't actually check for this error condition. All we
		// can observe is that the channel has been closed, so treat
		// this condition as the device does not support ABR.
		if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrPeerClosed {
			syslog.Warnf("img_writer: boot manager channel closed, assuming device does not support ABR")
			return nil, nil
		}

		return nil, fmt.Errorf("querying active config: %v", err)
	}

	if activeConfig.Which() == paver.BootManagerQueryActiveConfigurationResultResponse {
		syslog.Infof("img_writer: device supports ABR")
		return &activeConfig.Response.Configuration, nil
	}

	statusErr := zx.Status(activeConfig.Err)
	if statusErr == zx.ErrNotSupported {
		// this device doesn't support ABR, so fall back to the
		// legacy workflow.
		syslog.Infof("img_writer: device does not support ABR")
		return nil, nil
	}

	return nil, &zx.Error{Status: statusErr}
}

func calculateTargetConfig(activeConfig paver.Configuration) (*paver.Configuration, error) {
	var config paver.Configuration

	switch activeConfig {
	case paver.ConfigurationA:
		config = paver.ConfigurationB
	case paver.ConfigurationB:
		config = paver.ConfigurationA
	case paver.ConfigurationRecovery:
		syslog.Warnf("img_writer: configured for recovery, using partition A instead")
		config = paver.ConfigurationA
	default:
		return nil, fmt.Errorf("img_writer: unknown config: %s", activeConfig)
	}

	syslog.Infof("img_writer: writing to configuration %s", config)
	return &config, nil
}

func setConfigurationActive(bootManager *paver.BootManagerInterface, targetConfig paver.Configuration) error {
	syslog.Infof("img_writer: setting configuration %s active", targetConfig)
	status, err := bootManager.SetConfigurationActive(targetConfig)
	if err != nil {
		return err
	}
	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		return &zx.Error{Status: statusErr}
	}

	return nil
}

func writeAsset(svc *paver.DataSinkInterface, configuration paver.Configuration, asset paver.Asset, payload *mem.Buffer) error {
	syslog.Infof("img_writer: writing asset %q to %q", asset, configuration)

	status, err := svc.WriteAsset(configuration, asset, *payload)
	if err != nil {
		syslog.Errorf("img_writer: failed to write asset %q: %s", asset, err)
		return err
	}
	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		return &zx.Error{Status: statusErr}
	}
	return nil
}

func writeImg(svc *paver.DataSinkInterface, img string, imgsPath string, targetConfig *paver.Configuration) error {
	imgPath := filepath.Join(imgsPath, img)

	f, err := os.Open(imgPath)
	if err != nil {
		syslog.Warnf("img_writer: %q image not found, skipping", img)
		return nil
	}
	if fi, err := f.Stat(); err != nil || fi.Size() == 0 {
		syslog.Warnf("img_writer: %q zero length, skipping", img)
		return nil
	}
	defer f.Close()

	buffer, err := bufferForFile(f)
	if err != nil {
		return fmt.Errorf("img_writer: while getting vmo for %q: %q", img, err)
	}
	defer buffer.Vmo.Close()

	var writeImg func() error
	switch img {
	case "zbi", "zbi.signed":
		childVmo, err := buffer.Vmo.CreateChild(zx.VMOChildOptionCopyOnWrite|zx.VMOChildOptionResizable, 0, buffer.Size)
		if err != nil {
			return fmt.Errorf("img_writer: while getting vmo for %q: %q", img, err)
		}
		buffer2 := &mem.Buffer{
			Vmo:  childVmo,
			Size: buffer.Size,
		}
		defer buffer2.Vmo.Close()

		if targetConfig == nil {
			// device does not support ABR, so write the ZBI to the
			// A partition. We also try to write to the B partition
			// in order to be forwards compatible with devices that
			// will eventually support ABR, but we ignore errors
			// because some devices won't have a B partition.
			writeImg = func() error {
				if err := writeAsset(svc, paver.ConfigurationA, paver.AssetKernel, buffer); err != nil {
					return err
				}
				if err := writeAsset(svc, paver.ConfigurationB, paver.AssetKernel, buffer2); err != nil {
					asZxErr, ok := err.(*zx.Error)
					if ok && asZxErr.Status == zx.ErrNotSupported {
						syslog.Warnf("img_writer: skipping writing %q to B: %v", img, err)
					} else {
						return err
					}
				}
				return nil
			}
		} else {
			// device supports ABR, so only write the ZB to the
			// target partition.
			writeImg = func() error {
				return writeAsset(svc, *targetConfig, paver.AssetKernel, buffer)
			}
		}
	case "fuchsia.vbmeta":
		childVmo, err := buffer.Vmo.CreateChild(zx.VMOChildOptionCopyOnWrite|zx.VMOChildOptionResizable, 0, buffer.Size)
		if err != nil {
			return fmt.Errorf("img_writer: while getting vmo for %q: %q", img, err)
		}
		buffer2 := &mem.Buffer{
			Vmo:  childVmo,
			Size: buffer.Size,
		}
		defer buffer2.Vmo.Close()

		if targetConfig == nil {
			// device does not support ABR, so write vbmeta to the
			// A partition, and try to write to the B partiton. See
			// the comment in the zbi case for more details.
			if err := writeAsset(svc, paver.ConfigurationA,
				paver.AssetVerifiedBootMetadata, buffer); err != nil {
				return err
			}
			return writeAsset(svc, paver.ConfigurationB, paver.AssetVerifiedBootMetadata, buffer2)
		} else {
			// device supports ABR, so write the vbmeta to the
			// target partition.
			writeImg = func() error {
				return writeAsset(svc, *targetConfig, paver.AssetVerifiedBootMetadata, buffer2)
			}
		}
	case "zedboot", "zedboot.signed":
		writeImg = func() error {
			return writeAsset(svc, paver.ConfigurationRecovery, paver.AssetKernel, buffer)
		}
	case "recovery.vbmeta":
		writeImg = func() error {
			return writeAsset(svc, paver.ConfigurationRecovery, paver.AssetVerifiedBootMetadata, buffer)
		}
	case "bootloader":
		writeImg = func() error {
			status, err := svc.WriteBootloader(*buffer)
			if err != nil {
				return err
			}
			statusErr := zx.Status(status)
			if statusErr != zx.ErrOk {
				return fmt.Errorf("%s", statusErr)
			}
			return nil
		}
	case "board":
		return nil
	default:
		return fmt.Errorf("unrecognized image %q", img)
	}

	syslog.Infof("img_writer: writing %q from %q", img, imgPath)
	if err := writeImg(); err != nil {
		return fmt.Errorf("img_writer: error writing %q: %q", img, err)
	}
	syslog.Infof("img_writer: wrote %q successfully from %q", img, imgPath)

	return nil
}

func bufferForFile(f *os.File) (*mem.Buffer, error) {
	fio := syscall.FDIOForFD(int(f.Fd())).(*fdio.File)
	if fio == nil {
		return nil, fmt.Errorf("not fdio file")
	}

	status, buffer, err := fio.FileInterface().GetBuffer(zxio.VmoFlagRead)
	if err != nil {
		return nil, fmt.Errorf("GetBuffer fidl error: %q", err)
	}
	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		return nil, fmt.Errorf("GetBuffer error: %q", statusErr)
	}
	defer buffer.Vmo.Close()

	// VMOs acquired over FIDL are not guaranteed to be resizable, so create a child VMO that is.
	childVmo, err := buffer.Vmo.CreateChild(zx.VMOChildOptionCopyOnWrite|zx.VMOChildOptionResizable, 0, buffer.Size)
	if err != nil {
		return nil, err
	}

	return &mem.Buffer{
		Vmo:  childVmo,
		Size: buffer.Size,
	}, nil
}

// UpdateCurrentChannel persists the update channel info for a successful update
func UpdateCurrentChannel() error {
	targetPath := "/misc/ota/target_channel.json"
	contents, err := ioutil.ReadFile(targetPath)
	if err != nil {
		return fmt.Errorf("no target channel recorded in %v: %v", targetPath, err)
	}
	currentPath := "/misc/ota/current_channel.json"
	partPath := currentPath + ".part"
	f, err := os.Create(partPath)
	if err != nil {
		return fmt.Errorf("unable to write current channel to %v: %v", partPath, err)
	}
	defer f.Close()
	buf := bytes.NewBuffer(contents)
	_, err = buf.WriteTo(f)
	if err != nil {
		return fmt.Errorf("unable to write current channel to %v: %v", currentPath, err)
	}
	f.Sync()
	f.Close()
	if err := os.Rename(partPath, currentPath); err != nil {
		return fmt.Errorf("error moving %v to %v: %v", partPath, currentPath, err)
	}
	return nil
}
