// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"bufio"
	"bytes"
	"encoding/json"
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

func ConnectToPackageResolver(context *context.Context) (*pkg.PackageResolverWithCtxInterface, error) {
	req, pxy, err := pkg.NewPackageResolverWithCtxInterfaceRequest()

	if err != nil {
		syslog.Errorf("control interface could not be acquired: %s", err)
		return nil, err
	}

	context.ConnectToEnvService(req)
	return pxy, nil
}

func ConnectToPaver(context *context.Context) (*paver.DataSinkWithCtxInterface, *paver.BootManagerWithCtxInterface, error) {
	req, pxy, err := paver.NewPaverWithCtxInterfaceRequest()

	if err != nil {
		syslog.Errorf("control interface could not be acquired: %s", err)
		return nil, nil, err
	}
	defer pxy.Close()

	context.ConnectToEnvService(req)

	dataSinkReq, dataSinkPxy, err := paver.NewDataSinkWithCtxInterfaceRequest()
	if err != nil {
		syslog.Errorf("data sink interface could not be acquired: %s", err)
		return nil, nil, err
	}

	err = pxy.FindDataSink(fidl.Background(), dataSinkReq)
	if err != nil {
		syslog.Errorf("could not find data sink: %s", err)
		return nil, nil, err
	}

	bootManagerReq, bootManagerPxy, err := paver.NewBootManagerWithCtxInterfaceRequest()
	if err != nil {
		syslog.Errorf("boot manager interface could not be acquired: %s", err)
		return nil, nil, err
	}

	err = pxy.FindBootManager(fidl.Background(), bootManagerReq, false)
	if err != nil {
		syslog.Errorf("could not find boot manager: %s", err)
		return nil, nil, err
	}

	return dataSinkPxy, bootManagerPxy, nil
}

// CacheUpdatePackage caches the requested, possibly merkle-pinned, update
// package URL and returns the pkgfs path to the package.
func CacheUpdatePackage(updateURL string, resolver *pkg.PackageResolverWithCtxInterface) (*UpdatePackage, error) {
	dirPxy, err := resolvePackage(updateURL, resolver)
	if err != nil {
		return nil, err
	}
	pkg, err := NewUpdatePackage(dirPxy)
	if err != nil {
		return nil, err
	}

	merkle, err := pkg.Merkleroot()
	if err != nil {
		pkg.Close()
		return nil, err
	}
	syslog.Infof("resolved %s as %s", updateURL, merkle)

	return pkg, nil
}

// Packages deserializes the packages.json file in the system update package
type Packages struct {
	Version int `json:"version"`
	// A list of fully qualified URIs
	URIs []string `json:"content"`
}

func ParseRequirements(updatePkg *UpdatePackage) ([]string, []string, error) {
	// First, figure out which packages files we should parse
	parseJson := true
	pkgSrc, err := updatePkg.Open("packages.json")
	// Fall back to line formatted packages file if packages.json not present
	// Ideally, we'd fall back if specifically given the "file not found" error,
	// though it's unclear which error that is (syscall.ENOENT did not work)
	if err != nil {
		syslog.Infof("parse_requirements: could not open packages.json, falling back to packages.")
		parseJson = false
		pkgSrc, err = updatePkg.Open("packages")
	}
	if err != nil {
		return nil, nil, fmt.Errorf("error opening packages data file! %s", err)
	}
	defer pkgSrc.Close()

	// Now that we know which packages file to parse, we can parse it.
	pkgs := []string{}
	if parseJson {
		pkgs, err = ParsePackagesJson(pkgSrc)
	} else {
		pkgs, err = ParsePackagesLineFormatted(pkgSrc)
	}
	if err != nil {
		return nil, nil, fmt.Errorf("failed to parse packages: %v", err)
	}

	// Finally, we parse images
	imgSrc, err := os.Open(filepath.Join("/pkg", "data", "images"))
	if err != nil {
		return nil, nil, fmt.Errorf("error opening images data file! %s", err)
	}
	defer imgSrc.Close()

	imgs, err := ParseImages(imgSrc)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to parse images: %v", err)
	}

	return pkgs, imgs, nil
}

func ParsePackagesJson(pkgSrc io.ReadCloser) ([]string, error) {
	bytes, err := ioutil.ReadAll(pkgSrc)
	if err != nil {
		return nil, fmt.Errorf("failed to read packages.json with error: %v", err)
	}
	var packages Packages
	if err := json.Unmarshal(bytes, &packages); err != nil {
		return nil, fmt.Errorf("failed to unmarshal packages.json: %v", err)
	}
	if packages.Version != 1 {
		return nil, fmt.Errorf("unsupported version of packages.json: %v", packages.Version)
	}
	return packages.URIs, nil
}

func ParsePackagesLineFormatted(pkgSrc io.ReadCloser) ([]string, error) {
	pkgs := []string{}

	rdr := bufio.NewReader(pkgSrc)
	for {
		l, err := rdr.ReadString('\n')
		s := strings.TrimSpace(l)
		if (err == nil || err == io.EOF) && len(s) > 0 {
			entry := strings.Split(s, "=")
			if len(entry) != 2 {
				return nil, fmt.Errorf("parser: entry format %q", s)
			} else {
				pkgURI := fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s?hash=%s", entry[0], entry[1])
				pkgs = append(pkgs, pkgURI)
			}
		}

		if err != nil {
			if err != io.EOF {
				return nil, fmt.Errorf("parser: got error reading packages file %s", err)
			}
			break
		}
	}

	return pkgs, nil

}

func ParseImages(imgSrc io.ReadCloser) ([]string, error) {

	rdr := bufio.NewReader(imgSrc)
	imgs := []string{}

	for {
		l, err := rdr.ReadString('\n')
		s := strings.TrimSpace(l)
		if (err == nil || err == io.EOF) && len(s) > 0 {
			imgs = append(imgs, s)
		}

		if err != nil {
			if err != io.EOF {
				return nil, fmt.Errorf("parser: got error reading images file %s", err)
			}
			break
		}
	}

	return imgs, nil
}

func FetchPackages(pkgs []string, resolver *pkg.PackageResolverWithCtxInterface) error {
	var errCount int
	var firstErr error
	for _, pkgURI := range pkgs {
		if err := fetchPackage(pkgURI, resolver); err != nil {
			syslog.Errorf("fetch error: %s", err)
			errCount++
			if firstErr == nil {
				firstErr = err
			}
		}
	}

	if errCount > 0 {
		syslog.Errorf("system update failed, %d packages had errors", errCount)
		return firstErr
	}

	return nil
}

func fetchPackage(pkgURI string, resolver *pkg.PackageResolverWithCtxInterface) error {
	dirPxy, err := resolvePackage(pkgURI, resolver)
	if dirPxy != nil {
		dirPxy.Close(fidl.Background())
	}
	return err
}

func resolvePackage(pkgURI string, resolver *pkg.PackageResolverWithCtxInterface) (*fuchsiaio.DirectoryWithCtxInterface, error) {
	selectors := []string{}
	updatePolicy := pkg.UpdatePolicy{}
	dirReq, dirPxy, err := fuchsiaio.NewDirectoryWithCtxInterfaceRequest()
	if err != nil {
		return nil, err
	}

	syslog.Infof("requesting %s from update system", pkgURI)

	status, err := resolver.Resolve(fidl.Background(), pkgURI, selectors, updatePolicy, dirReq)
	if err != nil {
		dirPxy.Close(fidl.Background())
		return nil, fmt.Errorf("fetch: Resolve error: %s", err)
	}

	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		dirPxy.Close(fidl.Background())
		return nil, fmt.Errorf("fetch: Resolve status: %s", statusErr)
	}

	return dirPxy, nil
}

func ValidateUpdatePackage(updatePkg *UpdatePackage) error {
	actual, err := updatePkg.ReadFile("board")
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

	return nil
}

func ValidateImgs(imgs []string, updatePkg *UpdatePackage) error {
	// Require a 'zbi' or 'zbi.signed' partition in the update package.
	found := false
	for _, img := range []string{"zbi", "zbi.signed"} {
		if _, err := updatePkg.Stat(img); err == nil {
			found = true
			break
		}
	}

	if !found {
		return fmt.Errorf("parser: missing 'zbi' or 'zbi.signed'")
	}

	return nil
}

func WriteImgs(dataSink *paver.DataSinkWithCtxInterface, bootManager *paver.BootManagerWithCtxInterface, imgs []string, updatePkg *UpdatePackage) error {
	syslog.Infof("Writing images %+v from update package", imgs)

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
		if err := writeImg(dataSink, img, updatePkg, targetConfig); err != nil {
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
func queryActiveConfig(bootManager *paver.BootManagerWithCtxInterface) (*paver.Configuration, error) {
	activeConfig, err := bootManager.QueryActiveConfiguration(fidl.Background())
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

func setConfigurationActive(bootManager *paver.BootManagerWithCtxInterface, targetConfig paver.Configuration) error {
	syslog.Infof("img_writer: setting configuration %s active", targetConfig)
	status, err := bootManager.SetConfigurationActive(fidl.Background(), targetConfig)
	if err != nil {
		return err
	}
	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		return &zx.Error{Status: statusErr}
	}

	return nil
}

func writeAsset(svc *paver.DataSinkWithCtxInterface, configuration paver.Configuration, asset paver.Asset, payload *mem.Buffer) error {
	syslog.Infof("img_writer: writing asset %q to %q", asset, configuration)

	status, err := svc.WriteAsset(fidl.Background(), configuration, asset, *payload)
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

func writeImg(svc *paver.DataSinkWithCtxInterface, img string, updatePkg *UpdatePackage, targetConfig *paver.Configuration) error {
	f, err := updatePkg.Open(img)
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
			status, err := svc.WriteBootloader(fidl.Background(), *buffer)
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

	syslog.Infof("img_writer: writing %q from update package", img)
	if err := writeImg(); err != nil {
		return fmt.Errorf("img_writer: error writing %q: %q", img, err)
	}
	syslog.Infof("img_writer: wrote %q successfully", img)

	return nil
}

func bufferForFile(f *os.File) (*mem.Buffer, error) {
	fio := syscall.FDIOForFD(int(f.Fd())).(*fdio.File)
	if fio == nil {
		return nil, fmt.Errorf("not fdio file")
	}

	status, buffer, err := fio.GetBuffer(zxio.VmoFlagRead)
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
		return fmt.Errorf("no target channel recorded in %v: %w", targetPath, err)
	}
	currentPath := "/misc/ota/current_channel.json"
	partPath := currentPath + ".part"
	f, err := os.Create(partPath)
	if err != nil {
		return fmt.Errorf("unable to write current channel to %v: %w", partPath, err)
	}
	defer f.Close()
	buf := bytes.NewBuffer(contents)
	_, err = buf.WriteTo(f)
	if err != nil {
		return fmt.Errorf("unable to write current channel to %v: %w", currentPath, err)
	}
	f.Sync()
	f.Close()
	if err := os.Rename(partPath, currentPath); err != nil {
		return fmt.Errorf("error moving %v to %v: %w", partPath, currentPath, err)
	}
	return nil
}
