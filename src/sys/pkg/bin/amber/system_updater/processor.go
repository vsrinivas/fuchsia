// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"bufio"
	"bytes"
	"context"
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

	fuchsiaio "fidl/fuchsia/io"
	"fidl/fuchsia/mem"
	"fidl/fuchsia/paver"
	"fidl/fuchsia/pkg"

	"fuchsia.googlesource.com/syslog"
	"go.fuchsia.dev/fuchsia/src/lib/component"
)

// When this suffix is found in the "images" file, it indicates a typed image
// that looks for all matches within the update package.
const ImageTypeSuffix = "[_type]"

func ConnectToPackageResolver(ctx *component.Context) (*pkg.PackageResolverWithCtxInterface, error) {
	req, pxy, err := pkg.NewPackageResolverWithCtxInterfaceRequest()

	if err != nil {
		syslog.Errorf("control interface could not be acquired: %s", err)
		return nil, err
	}

	ctx.ConnectToEnvService(req)
	return pxy, nil
}

func ConnectToPaver(ctx *component.Context) (*paver.DataSinkWithCtxInterface, *paver.BootManagerWithCtxInterface, error) {
	req, pxy, err := paver.NewPaverWithCtxInterfaceRequest()

	if err != nil {
		syslog.Errorf("control interface could not be acquired: %s", err)
		return nil, nil, err
	}
	defer pxy.Close()

	ctx.ConnectToEnvService(req)

	dataSinkReq, dataSinkPxy, err := paver.NewDataSinkWithCtxInterfaceRequest()
	if err != nil {
		syslog.Errorf("data sink interface could not be acquired: %s", err)
		return nil, nil, err
	}

	err = pxy.FindDataSink(context.Background(), dataSinkReq)
	if err != nil {
		syslog.Errorf("could not find data sink: %s", err)
		return nil, nil, err
	}

	bootManagerReq, bootManagerPxy, err := paver.NewBootManagerWithCtxInterfaceRequest()
	if err != nil {
		syslog.Errorf("boot manager interface could not be acquired: %s", err)
		return nil, nil, err
	}

	err = pxy.FindBootManager(context.Background(), bootManagerReq)
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

// An image name and type string.
type Image struct {
	// The base name of the image.
	Name string

	// A type string, default "".
	Type string
}

// Returns an Image's filename in an update package.
//
// If a type is given, the filename in the package will be <name>_<type>, e.g.:
//   name="foo", type="" -> "foo"
//   name="foo", type="bar" -> "foo_bar"
func (i *Image) Filename() string {
	if i.Type == "" {
		return i.Name
	}
	return fmt.Sprintf("%s_%s", i.Name, i.Type)
}

func ParseRequirements(updatePkg *UpdatePackage) ([]string, []Image, error) {
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

	filenames, err := updatePkg.ListFiles()
	if err != nil {
		return nil, nil, fmt.Errorf("failed to list package files: %v", err)
	}

	imgs, err := ParseImages(imgSrc, filenames)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to parse images: %v", err)
	}

	return pkgs, imgs, nil
}

// Packages deserializes the packages.json file in the system update package.
// NOTE: Fields must be exported for json decoding.
type packages struct {
	Version intOrStr `json:"version"`
	// A list of fully qualified URIs.
	URIs []string `json:"content"`
}

type intOrStr int

// Enables us to support version as either a string or int.
func (i *intOrStr) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err == nil {
		b = []byte(s)
	}

	return json.Unmarshal(b, (*int)(i))
}

func ParsePackagesJson(pkgSrc io.ReadCloser) ([]string, error) {
	bytes, err := ioutil.ReadAll(pkgSrc)
	if err != nil {
		return nil, fmt.Errorf("failed to read packages.json with error: %v", err)
	}
	var packages packages
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

// Finds all images that match |basename| in |filenames|.
//
// A match is one of:
//   <basename>
//   <basename>_<type>
func FindTypedImages(basename string, filenames []string) []Image {
	var images []Image
	for _, name := range filenames {
		if strings.HasPrefix(name, basename) {
			suffix := name[len(basename):]

			if len(suffix) == 0 {
				// The base name alone indicates default type (empty string).
				images = append(images, Image{Name: basename, Type: ""})
			} else if suffix[0] == '_' {
				images = append(images, Image{Name: basename, Type: suffix[1:]})
			}
		}
	}

	return images
}

// Returns a list of images derived from the "images" file.
//
// Untyped images (those without the [_type] suffix) are included in the return
// slice no matter what.
//
// Typed images, on the other hand, will only include matches that exist in
// |filenames|.
func ParseImages(imgSrc io.ReadCloser, filenames []string) ([]Image, error) {

	rdr := bufio.NewReader(imgSrc)
	imgs := []Image{}

	for {
		l, err := rdr.ReadString('\n')
		s := strings.TrimSpace(l)
		if (err == nil || err == io.EOF) && len(s) > 0 {
			if strings.HasSuffix(s, ImageTypeSuffix) {
				// Typed image: look for all matching images in the package.
				basename := strings.TrimSuffix(s, ImageTypeSuffix)
				imgs = append(imgs, FindTypedImages(basename, filenames)...)
			} else {
				imgs = append(imgs, Image{Name: s, Type: ""})
			}
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

// Types to deserialize the update-mode file. NOTE: Fields must be exported for json decoding.
// Expected form for update-mode file is:
// {
//   "version": "1",
//   "content": {
//     "mode": "normal" / "force-recovery",
//   }
// }
type updateModeFileContent struct {
	Mode string `json:"mode"`
}
type updateModeFile struct {
	Version string                `json:"version"`
	Content updateModeFileContent `json:"content"`
}

// Type to describe the supported update modes.
// Note: exporting since this will be used in main (to be consistent with the rest of the code).
type UpdateMode string

const (
	UpdateModeNormal        UpdateMode = "normal"
	UpdateModeForceRecovery            = "force-recovery"
)

// We define custom error wrappers so we can test the proper error is being returned.
type updateModeNotSupportedError UpdateMode

func (e updateModeNotSupportedError) Error() string {
	return fmt.Sprintf("unsupported update mode: %s", string(e))
}

type jsonUnmarshalError struct {
	err error
}

func (e jsonUnmarshalError) Error() string {
	return fmt.Sprintf("failed to unmarshal update-mode: %v", e.err)
}

// Note: exporting since this will be used in main (to be consistent with the rest of the code).
func ParseUpdateMode(updatePkg *UpdatePackage) (UpdateMode, error) {
	// Fall back to normal if the update-mode file does not exist.
	// Ideally, we'd fall back if specifically given the "file not found" error,
	// though it's unclear which error that is (syscall.ENOENT did not work).
	modeSrc, err := updatePkg.Open("update-mode")
	if err != nil {
		syslog.Infof("parse_update_mode: could not open update-mode file, assuming normal system update flow.")
		return UpdateModeNormal, nil
	}
	defer modeSrc.Close()
	// Read the raw bytes.
	b, err := ioutil.ReadAll(modeSrc)
	if err != nil {
		return "", fmt.Errorf("failed to read mode file: %w", err)
	}
	// Convert to json.
	var updateModeFile updateModeFile
	if err := json.Unmarshal(b, &updateModeFile); err != nil {
		return "", jsonUnmarshalError{err}
	}
	// Confirm we support this mode.
	mode := UpdateMode(updateModeFile.Content.Mode)
	if mode != UpdateModeNormal && mode != UpdateModeForceRecovery {
		return "", updateModeNotSupportedError(mode)
	}
	return mode, nil
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
		dirPxy.Close(context.Background())
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

	status, err := resolver.Resolve(context.Background(), pkgURI, selectors, updatePolicy, dirReq)
	if err != nil {
		dirPxy.Close(context.Background())
		return nil, fmt.Errorf("fetch: Resolve error: %s", err)
	}

	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		dirPxy.Close(context.Background())
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

func ValidateImgs(imgs []Image, updatePkg *UpdatePackage, updateMode UpdateMode) error {
	found := false
	for _, img := range []string{"zbi", "zbi.signed"} {
		if _, err := updatePkg.Stat(img); err == nil {
			found = true
			break
		}
	}

	// Update package with normal mode should have a `zbi` or `zbi.signed`.
	if updateMode == UpdateModeNormal && !found {
		return fmt.Errorf("parser: missing 'zbi' or 'zbi.signed', this is required in normal update mode")
	}

	// Update package with force-recovery mode should NOT have a `zbi` nor `zbi.signed`.
	if updateMode == UpdateModeForceRecovery && found {
		return fmt.Errorf("parser: contains 'zbi' or 'zbi.signed', this is not allowed in force-recovery update mode")
	}

	return nil
}

func WriteImgs(dataSink *paver.DataSinkWithCtxInterface, bootManager *paver.BootManagerWithCtxInterface, imgs []Image, updatePkg *UpdatePackage, updateMode UpdateMode, skipRecovery bool) error {
	if updateMode == UpdateModeForceRecovery && skipRecovery == true {
		return fmt.Errorf("can't force recovery when skipping recovery image installation")
	}
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
		if err := writeImg(dataSink, img, updatePkg, targetConfig, skipRecovery); err != nil {
			return err
		}
	}

	if updateMode == UpdateModeNormal && targetConfig != nil {
		if err := setConfigurationActive(bootManager, *targetConfig); err != nil {
			return err
		}
	} else if updateMode == UpdateModeForceRecovery {
		for _, config := range []paver.Configuration{paver.ConfigurationA, paver.ConfigurationB} {
			if err := setConfigurationUnbootable(bootManager, config); err != nil {
				return fmt.Errorf("failed to set configuration unbootable: %v", err)
			}
		}
	}

	if err = flushDataSink(dataSink); err != nil {
		return fmt.Errorf("img_writer: failed to flush data sink. %v", err)
	}

	if targetConfig != nil {
		if err = flushBootManager(bootManager); err != nil {
			return fmt.Errorf("img_writer: failed to flush boot manager. %v", err)
		}
	}

	return nil
}

func flushDataSink(dataSink *paver.DataSinkWithCtxInterface) error {
	status, err := dataSink.Flush(context.Background())

	if err != nil {
		return err
	}

	if statusErr := zx.Status(status); statusErr != zx.ErrOk {
		return &zx.Error{Status: statusErr}
	}

	return nil
}

func flushBootManager(bootManager *paver.BootManagerWithCtxInterface) error {
	status, err := bootManager.Flush(context.Background())

	if err != nil {
		return err
	}

	if statusErr := zx.Status(status); statusErr != zx.ErrOk {
		return &zx.Error{Status: statusErr}
	}

	return nil
}

// queryActiveConfig asks the boot manager what partition the device booted
// from. If the device does not support ABR, it returns nil as the
// configuration.
func queryActiveConfig(bootManager *paver.BootManagerWithCtxInterface) (*paver.Configuration, error) {
	activeConfig, err := bootManager.QueryActiveConfiguration(context.Background())
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
	status, err := bootManager.SetConfigurationActive(context.Background(), targetConfig)
	if err != nil {
		return err
	}
	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		return &zx.Error{Status: statusErr}
	}

	return nil
}

func setConfigurationUnbootable(bootManager *paver.BootManagerWithCtxInterface, targetConfig paver.Configuration) error {
	syslog.Infof("img_writer: setting configuration %s unbootable", targetConfig)
	status, err := bootManager.SetConfigurationUnbootable(context.Background(), targetConfig)
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

	status, err := svc.WriteAsset(context.Background(), configuration, asset, *payload)
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

func writeImg(svc *paver.DataSinkWithCtxInterface, img Image, updatePkg *UpdatePackage, targetConfig *paver.Configuration, skipRecovery bool) error {
	f, err := updatePkg.Open(img.Filename())
	if err != nil {
		syslog.Warnf("img_writer: %q image not found, skipping", img.Filename())
		return nil
	}
	if fi, err := f.Stat(); err != nil || fi.Size() == 0 {
		syslog.Warnf("img_writer: %q zero length, skipping", img.Filename())
		return nil
	}
	defer f.Close()

	buffer, err := bufferForFile(f)
	if err != nil {
		return fmt.Errorf("img_writer: while getting vmo for %q: %q", img.Filename(), err)
	}
	defer buffer.Vmo.Close()

	var writeImg func() error
	switch img.Name {
	case "zbi", "zbi.signed":
		childVmo, err := buffer.Vmo.CreateChild(zx.VMOChildOptionCopyOnWrite|zx.VMOChildOptionResizable, 0, buffer.Size)
		if err != nil {
			return fmt.Errorf("img_writer: while getting vmo for %q: %q", img.Filename(), err)
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
						syslog.Warnf("img_writer: skipping writing %q to B: %v", img.Filename(), err)
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
			return fmt.Errorf("img_writer: while getting vmo for %q: %q", img.Filename(), err)
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
	// TODO(52356): drop "zedboot" names.
	case "zedboot", "zedboot.signed", "recovery":
		if skipRecovery {
			return nil
		} else {
			writeImg = func() error {
				return writeAsset(svc, paver.ConfigurationRecovery, paver.AssetKernel, buffer)
			}
		}
	case "recovery.vbmeta":
		if skipRecovery {
			return nil
		} else {
			writeImg = func() error {
				return writeAsset(svc, paver.ConfigurationRecovery, paver.AssetVerifiedBootMetadata, buffer)
			}
		}
	case "bootloader":
		// Keep support for update packages still using the older "bootloader"
		// file, which is handled identically to "firmware" but without type
		// support so img.Type will always be "".
		fallthrough
	case "firmware":
		writeImg = func() error {
			result, err := svc.WriteFirmware(context.Background(), img.Type, *buffer)
			if err != nil {
				return err
			}

			if result.Which() == paver.WriteFirmwareResultUnsupportedType {
				syslog.Infof("img_writer: skipping unsupported firmware type %q", img.Type)
				// Return nil here to skip unsupported types rather than failing.
				// This lets us add new types in the future without breaking
				// the update flow from older devices.
				return nil
			}

			statusErr := zx.Status(result.Status)
			if statusErr != zx.ErrOk {
				return fmt.Errorf("%s", statusErr)
			}
			return nil
		}
	case "board":
		return nil
	default:
		return fmt.Errorf("unrecognized image %q", img.Filename())
	}

	syslog.Infof("img_writer: writing %q from update package", img.Filename())
	if err := writeImg(); err != nil {
		return fmt.Errorf("img_writer: error writing %q: %q", img.Filename(), err)
	}
	syslog.Infof("img_writer: wrote %q successfully", img.Filename())

	return nil
}

func bufferForFile(f *os.File) (*mem.Buffer, error) {
	fio := syscall.FDIOForFD(int(f.Fd())).(*fdio.File)
	if fio == nil {
		return nil, fmt.Errorf("not fdio file")
	}

	status, buffer, err := fio.GetBuffer(fuchsiaio.VmoFlagRead)
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
