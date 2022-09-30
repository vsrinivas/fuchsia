// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sdkcommon

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"
)

var (
	// ExecCommand exports exec.Command as a variable so it can be mocked.
	ExecCommand = exec.Command
	// ExecLookPath exported to support mocking.
	ExecLookPath = exec.LookPath
	// logging support.
	logLevel = logger.InfoLevel
	log      = logger.NewLogger(logLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "sdk ")
)

// FuchsiaDevice represent a Fuchsia device.
type FuchsiaDevice struct {
	// SSH address of the Fuchsia device.
	SSHAddr string
	// Nodename of the Fuchsia device.
	Name string
}

// deviceInfo represents targets that the ffx daemon currently has in memory.
type deviceInfo struct {
	Nodename    string   `json:"nodename"`
	RCSState    string   `json:"rcs_state"`
	Serial      string   `json:"serial"`
	TargetType  string   `json:"target_type"`
	TargetState string   `json:"target_state"`
	Addresses   []string `json:"addresses"`
}

// GCSImage is used to return the bucket, name and version of a prebuilt.
type GCSImage struct {
	Bucket  string
	Name    string
	Version string
}

// Property keys used to get and set device configuration
const (
	DeviceNameKey  string = "device-name"
	BucketKey      string = "bucket"
	ImageKey       string = "image"
	DeviceIPKey    string = "device-ip"
	SSHPortKey     string = "ssh-port"
	PackageRepoKey string = "package-repo"
	PackagePortKey string = "package-port"
	DefaultKey     string = "default"
	// Top level key used to store device configurations in user level.
	deviceConfigurationKey string = "device_config"
	// Deprecated - top level key for storing device configurations in global level.
	globalDeviceConfigurationKey string = "DeviceConfiguration"
	// Deprecated - key used to identify the default device in global level.
	defaultDeviceKey string = "_DEFAULT_DEVICE_"

	sleepTimeInSeconds = 5
	UnknownTargetName  = "unknown"
	maxRetryCount      = 3
)

const (
	defaultBucketName  string = "fuchsia"
	DefaultSSHPort     string = "22"
	defaultPackagePort string = "8083"
	helpfulTipMsg      string = `Try running 'ffx target list' and then 'ffx config set device_config.<device-name>.image <image_name>'.
	If you have more than one device listed, use 'ffx target default set <device-name>' to set a default device.`
)

var validPropertyNames = [...]string{
	DeviceNameKey,
	BucketKey,
	ImageKey,
	DeviceIPKey,
	SSHPortKey,
	PackageRepoKey,
	PackagePortKey,
	DefaultKey,
}

// DeviceConfig holds all the properties that are configured
// for a given device.
type DeviceConfig struct {
	DeviceName   string `json:"device-name"`
	Bucket       string `json:"bucket"`
	Image        string `json:"image"`
	DeviceIP     string `json:"device-ip"`
	SSHPort      string `json:"ssh-port"`
	PackageRepo  string `json:"package-repo"`
	PackagePort  string `json:"package-port"`
	IsDefault    bool   `json:"default"`
	Discoverable bool   `json:"discoverable"`
}

// SDKProperties holds the common data for SDK tools.
// These values should be set or initialized by calling
// New().
type SDKProperties struct {
	dataPath string
	version  string
}

func (sdk SDKProperties) setDeviceDefaults(deviceConfig *DeviceConfig) DeviceConfig {
	// no reasonable default for device-name
	if deviceConfig.Bucket == "" {
		deviceConfig.Bucket = defaultBucketName
	}
	// no reasonable default for image
	// no reasonable default for device-ip
	if deviceConfig.SSHPort == "" {
		deviceConfig.SSHPort = DefaultSSHPort
	}
	if deviceConfig.PackageRepo == "" {
		deviceConfig.PackageRepo = sdk.getDefaultPackageRepoDir(deviceConfig.DeviceName)
	}
	if deviceConfig.PackagePort == "" {
		deviceConfig.PackagePort = defaultPackagePort
	}
	return *deviceConfig
}

// Builds the data key for the given segments.
func getDeviceDataKey(segments []string, isGlobal bool) string {
	fullKey := []string{deviceConfigurationKey}
	if isGlobal {
		fullKey = []string{globalDeviceConfigurationKey}
	}
	return strings.Join(append(fullKey, segments...), ".")
}

// DefaultGetUserHomeDir is the default implementation of GetUserHomeDir()
// to allow mocking of user.Current()
func DefaultGetUserHomeDir() (string, error) {
	usr, err := user.Current()
	if err != nil {
		return "", nil
	}
	return usr.HomeDir, nil
}

// DefaultGetUsername is the default implementation of GetUsername()
// to allow mocking of user.Current()
func DefaultGetUsername() (string, error) {
	usr, err := user.Current()
	if err != nil {
		return "", nil
	}
	return usr.Username, nil
}

// DefaultGetHostname is the default implementation of GetHostname()
// to allow mocking of user.Current()
func DefaultGetHostname() (string, error) {
	return os.Hostname()
}

var (
	// GetUserHomeDir to allow mocking.
	GetUserHomeDir = DefaultGetUserHomeDir

	// GetUsername to allow mocking.
	GetUsername = DefaultGetUsername

	// GetHostname to allow mocking.
	GetHostname = DefaultGetHostname
)

func NewWithDataPath(dataPath string) (SDKProperties, error) {
	sdk := SDKProperties{}

	if dataPath != "" {
		sdk.dataPath = dataPath
	} else {
		homeDir, err := GetUserHomeDir()
		if err != nil {
			return sdk, err
		}
		sdk.dataPath = filepath.Join(homeDir, ".fuchsia")
		if !FileExists(sdk.dataPath) {
			if err := os.MkdirAll(sdk.dataPath, os.ModePerm); err != nil {
				return sdk, err
			}
		}
	}

	toolsDir, err := sdk.GetToolsDir()
	if err != nil {
		return sdk, err
	}
	manifestFile, err := filepath.Abs(filepath.Join(toolsDir, "..", "..", "meta", "manifest.json"))
	if err != nil {
		return sdk, err
	}
	// If this is running in-tree, the manifest may not exist.
	if FileExists(manifestFile) {
		if sdk.version, err = readSDKVersion(manifestFile); err != nil {
			return sdk, err
		}
	}

	return sdk, err
}

// New creates an initialized SDKProperties using the default location
// for the data directory.
func New() (SDKProperties, error) {
	return NewWithDataPath("")
}

// GetSDKVersion returns the version of the SDK or empty if not set.
// Use sdkcommon.New() to create an initialized SDKProperties struct.
func (sdk SDKProperties) GetSDKVersion() string {
	return sdk.version
}

// GetSDKDataPath returns the path to the directory for storing SDK related data,
//
//	or empty if not set.
//
// Use sdkcommon.New() to create an initialized SDKProperties struct.
func (sdk SDKProperties) GetSDKDataPath() string {
	return sdk.dataPath
}

// getSDKVersion reads the manifest JSON file and returns the "id" property.
func readSDKVersion(manifestFilePath string) (string, error) {
	manifestFile, err := os.Open(manifestFilePath)
	// if we os.Open returns an error then handle it
	if err != nil {
		return "", err
	}
	defer manifestFile.Close()
	data, err := io.ReadAll(manifestFile)
	if err != nil {
		return "", err
	}

	var result map[string]interface{}
	if err := json.Unmarshal([]byte(data), &result); err != nil {
		return "", err
	}

	version, _ := result["id"].(string)
	return version, nil
}

// GetDefaultPackageRepoDir returns the path to the package repository.
// If the value has been set with `ffx`, use that value.
// Otherwise if there is a default target defined, return the target
// specific path.
// Lastly, if there is nothing, return the default repo path.
func (sdk SDKProperties) getDefaultPackageRepoDir(deviceName string) string {
	if deviceName != "" {
		return filepath.Join(sdk.GetSDKDataPath(), deviceName,
			"packages", "amber-files")
	}
	// As a last resort, `ffx` and the data are working as intended,
	// but no default has been configured, so fall back to the generic
	// legacy path.
	return filepath.Join(sdk.GetSDKDataPath(), "packages", "amber-files")
}

// GetToolsDir returns the path to the SDK tools for the current
// CPU architecture. This is implemented by default of getting the
// directory of the currently exeecuting binary.
func (sdk SDKProperties) GetToolsDir() (string, error) {
	exePath, err := os.Executable()
	if err != nil {
		return "", fmt.Errorf("Could not currently running file: %v", err)
	}
	dir, err := filepath.Abs(filepath.Dir(exePath))
	if err != nil {
		return "", fmt.Errorf("Could not get directory of currently running file: %s", err)
	}

	// This could be a symlink in a directory, so look for another common
	// tool (ffx). If it does not, try using the dir from argv[0].
	if FileExists(filepath.Join(dir, "ffx")) {
		return dir, nil
	}

	dir, err = filepath.Abs(filepath.Dir(os.Args[0]))
	if err != nil {
		return "", fmt.Errorf("Could not get path of argv[0]: %v", err)
	}
	return dir, nil
}

// GetAvailableImages returns the images available for the given version and bucket. If
// bucket is not the default bucket, the images in the default bucket are also returned.
func (sdk SDKProperties) GetAvailableImages(version string, bucket string) ([]GCSImage, error) {
	var buckets []string
	var images []GCSImage

	if bucket == "" || bucket == defaultBucketName {
		buckets = []string{defaultBucketName}
	} else {
		buckets = []string{bucket, defaultBucketName}
	}

	for _, b := range buckets {
		url := fmt.Sprintf("gs://%v/development/%v/images*", b, version)
		args := []string{"ls", url}
		output, err := runGSUtil(args)
		if err != nil {
			return images, err
		}
		for _, line := range strings.Split(strings.TrimSuffix(string(output), "\n"), "\n") {
			if len(filepath.Base(line)) >= 4 {
				bucketVersion := filepath.Base(filepath.Dir(filepath.Dir(line)))
				name := filepath.Base(line)[:len(filepath.Base(line))-4]
				images = append(images, GCSImage{Bucket: b, Version: bucketVersion, Name: name})
			} else {
				log.Warningf("Could not parse image name: %v", line)
			}
		}
	}
	return images, nil
}

// GetPackageSourcePath returns the GCS path for the given values.
func (sdk SDKProperties) GetPackageSourcePath(version string, bucket string, image string) string {
	return fmt.Sprintf("gs://%s/development/%s/packages/%s.tar.gz", bucket, version, image)
}

// RunFFXDoctor runs common checks for the ffx tool and host environment and returns
// the stdout.
func (sdk SDKProperties) RunFFXDoctor() (string, error) {
	args := []string{"doctor"}
	return sdk.RunFFX(args, false)
}

func (f *FuchsiaDevice) String() string {
	return fmt.Sprintf("%s %s", f.SSHAddr, f.Name)
}

func (f *FuchsiaDevice) getIPAddressAndPort() (string, string) {
	host, port, err := net.SplitHostPort(f.SSHAddr)
	if err != nil {
		log.Debugf("Got an error from net.SplitHostPort(%#v): %v", f.SSHAddr, err)
		return "", ""
	}
	return host, port
}

// isUnknownInListOutput returns true if unknown is in the ffx output.
func (sdk SDKProperties) isUnknownInListOutput(discoveredDevices []*deviceInfo) bool {
	for _, currentDevice := range discoveredDevices {
		if strings.Contains(strings.TrimSpace(currentDevice.Nodename), UnknownTargetName) {
			return true
		}
	}
	return false
}

func (sdk SDKProperties) listDevicesWithFFX() ([]*deviceInfo, error) {
	args := []string{"--machine", "json", "target", "list"}
	output, err := sdk.RunFFX(args, false)
	if err != nil {
		return nil, fmt.Errorf("Unable to list devices, please try running 'ffx doctor': %v", err)
	}
	var discoveredDevices []*deviceInfo
	if len(output) == 0 {
		return discoveredDevices, nil
	}
	if err := json.Unmarshal([]byte(output), &discoveredDevices); err != nil {
		return nil, fmt.Errorf("Unable to unmarshal device info from ffx, please try running 'ffx doctor': %v", err)
	}

	return discoveredDevices, nil
}

// listDevices returns all available fuchsia devices.
func (sdk SDKProperties) listDevices() ([]*FuchsiaDevice, error) {
	var discoveredDevices []*deviceInfo
	var err error
	// List the devices using ffx. If unknown is in the output from ffx, we will try
	// `maxRetryCount` so that the device will show up with the name.
	// If after the `maxRetryCount` is reached and unknown is still in the output, the device
	// is unreachable.
	for tries := 0; tries < maxRetryCount; tries++ {
		discoveredDevices, err = sdk.listDevicesWithFFX()
		if err != nil {
			return nil, err
		}
		if !sdk.isUnknownInListOutput(discoveredDevices) {
			break
		}
		// This should only occur when the device is in unknown state, usually in the first
		// invocation of any of the f* tools.
		time.Sleep(sleepTimeInSeconds * time.Second)
	}

	var devices []*FuchsiaDevice

	for _, currentDevice := range discoveredDevices {
		if len(currentDevice.Addresses) == 0 {
			continue
		}
		sshAddr, err := sdk.getDeviceSSHAddress(currentDevice)
		// If we are unable to get the device ssh address, skip the device.
		if err != nil {
			log.Debugf("Failed to getDeviceSSHAddress for %s: %v", currentDevice.Nodename, err)
			sshAddr = ""
			continue
		}
		devices = append(devices, &FuchsiaDevice{
			SSHAddr: strings.TrimSpace(sshAddr),
			Name:    strings.TrimSpace(currentDevice.Nodename),
		})
	}
	return devices, nil
}

func (sdk SDKProperties) getDefaultFFXDevice() (string, error) {
	args := []string{"target", "default", "get"}
	output, err := sdk.RunFFX(args, false)
	if err != nil {
		return "", fmt.Errorf("Unable to get ffx default device, please try running 'ffx doctor': %v", err)
	}
	log.Debugf("FFX default device is: %v", output)
	return strings.TrimSpace(output), nil
}

func (sdk SDKProperties) getDeviceSSHAddress(device *deviceInfo) (string, error) {
	args := []string{"--target", device.Nodename, "target", "get-ssh-address"}
	output, err := sdk.RunFFX(args, false)
	if err != nil {
		return "", fmt.Errorf("Unable to get ssh address: %v", err)
	}
	return strings.TrimSpace(output), nil
}

func getCommonSSHArgs(sdk SDKProperties, customSSHConfig string, privateKey string,
	sshPort string) []string {

	var cmdArgs []string
	if customSSHConfig != "" {
		cmdArgs = append(cmdArgs, "-F", customSSHConfig)
	} else {
		cmdArgs = append(cmdArgs, "-F", getFuchsiaSSHConfigFile(sdk))
	}
	if privateKey != "" {
		cmdArgs = append(cmdArgs, "-i", privateKey)
	}
	if sshPort != "" {
		cmdArgs = append(cmdArgs, "-p", sshPort)
	}

	return cmdArgs
}

// RunSFTPCommand runs sftp (one of SSH's file copy tools).
// Setting toTarget to true will copy file SRC from host to DST on the target.
// Otherwise it will copy file from SRC from target to DST on the host.
// sshPort if non-empty will use this port to connect to the device.
// The return value is the error if any.
func (sdk SDKProperties) RunSFTPCommand(targetAddress string, customSSHConfig string, privateKey string,
	sshPort string, toTarget bool, src string, dst string) error {

	commonArgs := []string{"-q", "-b", "-"}
	if customSSHConfig == "" || privateKey == "" {
		if err := checkSSHConfig(sdk); err != nil {
			return err
		}
	}

	cmdArgs := getCommonSSHArgs(sdk, customSSHConfig, privateKey, sshPort)

	cmdArgs = append(cmdArgs, commonArgs...)
	if targetAddress == "" {
		return errors.New("target address must be specified")
	}
	// SFTP needs the [] around the ipv6 address, which is different than ssh.
	if strings.Contains(targetAddress, ":") {
		targetAddress = fmt.Sprintf("[%v]", targetAddress)
	}
	cmdArgs = append(cmdArgs, targetAddress)

	stdin := ""

	if toTarget {
		stdin = fmt.Sprintf("put %v %v", src, dst)
	} else {
		stdin = fmt.Sprintf("get %v %v", src, dst)
	}

	return runSFTP(cmdArgs, stdin)
}

// RunSSHCommand runs the command provided in args on the given target device.
// The customSSHconfig is optional and overrides the SSH configuration defined by the SDK.
// privateKey is optional to specify a private key to use to access the device.
// verbose adds the -v flag to ssh.
// sshPort if non-empty is used as the custom ssh port on the commandline.
// The return value is the stdout.
func (sdk SDKProperties) RunSSHCommand(targetAddress string, customSSHConfig string,
	privateKey string, sshPort string, verbose bool, args []string) (string, error) {

	cmdArgs, err := buildSSHArgs(sdk, targetAddress, customSSHConfig, privateKey, sshPort, verbose, args)
	if err != nil {
		return "", err
	}

	return runSSH(cmdArgs, false)
}

// RunSSHShell runs the command provided in args on the given target device and
// uses the system stdin, stdout, stderr.
// Returns when the ssh process exits.
// The customSSHconfig is optional and overrides the SSH configuration defined by the SDK.
// privateKey is optional to specify a private key to use to access the device.
// sshPort if non-empty is used as the custom ssh port on the commandline.
// verbose adds the -v flag to ssh.
// The return value is the stdout.
func (sdk SDKProperties) RunSSHShell(targetAddress string, customSSHConfig string,
	privateKey string, sshPort string, verbose bool, args []string) error {

	cmdArgs, err := buildSSHArgs(sdk, targetAddress, customSSHConfig, privateKey,
		sshPort, verbose, args)
	if err != nil {
		return err
	}
	_, err = runSSH(cmdArgs, true)
	return err

}

func buildSSHArgs(sdk SDKProperties, targetAddress string, customSSHConfig string,
	privateKey string, sshPort string, verbose bool, args []string) ([]string, error) {
	if customSSHConfig == "" || privateKey == "" {
		if err := checkSSHConfig(sdk); err != nil {
			return []string{}, err
		}
	}

	cmdArgs := getCommonSSHArgs(sdk, customSSHConfig, privateKey, sshPort)
	if verbose {
		cmdArgs = append(cmdArgs, "-v")
	}

	if targetAddress == "" {
		return cmdArgs, errors.New("target address must be specified")
	}
	cmdArgs = append(cmdArgs, targetAddress)

	cmdArgs = append(cmdArgs, args...)

	return cmdArgs, nil
}

func getFuchsiaSSHConfigFile(sdk SDKProperties) string {
	return filepath.Join(sdk.GetSDKDataPath(), "sshconfig")
}

/* This function creates the ssh keys needed to
 work with devices running Fuchsia. There are two parts, the keys and the config.

 There is a key for Fuchsia that is placed in a well-known location so that applications
 which need to access the Fuchsia device can all use the same key. This is stored in
 ${HOME}/.ssh/fuchsia_ed25519.

 The authorized key file used for paving is in ${HOME}/.ssh/fuchsia_authorized_keys.
 The private key used when ssh'ing to the device is in ${HOME}/.ssh/fuchsia_ed25519.


 The second part of is the sshconfig file used by the SDK when using SSH.
 This is stored in the Fuchsia SDK data directory named sshconfig.
 This script checks for the private key file being referenced in the sshconfig and
the matching version tag. If they are not present, the sshconfig file is regenerated.
*/

const sshConfigTag = "Fuchsia SDK config version 5 tag"

func checkSSHConfig(sdk SDKProperties) error {
	// The ssh configuration should not be modified.

	homeDir, err := GetUserHomeDir()
	if err != nil {
		return fmt.Errorf("SSH configuration requires a $HOME directory: %v", err)
	}
	userName, err := GetUsername()
	if err != nil {
		return fmt.Errorf("SSH configuration requires a user name: %v", err)
	}
	var (
		sshDir        = filepath.Join(homeDir, ".ssh")
		authFile      = filepath.Join(sshDir, "fuchsia_authorized_keys")
		keyFile       = filepath.Join(sshDir, "fuchsia_ed25519")
		sshConfigFile = getFuchsiaSSHConfigFile(sdk)
	)
	// If the public and private key pair exist, and the sshconfig
	// file is up to date, then our work here is done, return success.
	if FileExists(authFile) && FileExists(keyFile) && FileExists(sshConfigFile) {
		config, err := os.ReadFile(sshConfigFile)
		if err == nil {
			if strings.Contains(string(config), sshConfigTag) {
				return nil
			}
		}
		// The version tag does not match, so remove the old config file.
		os.Remove(sshConfigFile)
	}

	if err := os.MkdirAll(sshDir, 0755); err != nil {
		return fmt.Errorf("Could not create %v: %v", sshDir, err)
	}

	// Check to migrate keys from old location
	if !FileExists(authFile) || !FileExists(keyFile) {
		if err := moveLegacyKeys(sdk, authFile, keyFile); err != nil {
			return fmt.Errorf("Could not migrate legacy SSH keys: %v", err)
		}
	}

	// Create keys if needed
	if !FileExists(authFile) || !FileExists(keyFile) {
		if !FileExists(keyFile) {
			hostname, _ := GetHostname()
			if hostname == "" {
				hostname = "unknown"
			}
			if err := generateSSHKey(keyFile, userName, hostname); err != nil {
				return fmt.Errorf("Could not generate private SSH key: %v", err)
			}
		}
		if err := generatePublicSSHKeyfile(keyFile, authFile); err != nil {
			return fmt.Errorf("Could not get public keys from private SSH key: %v", err)
		}
	}

	if err := writeSSHConfigFile(sshConfigFile, sshConfigTag, keyFile); err != nil {
		return fmt.Errorf("Could not write sshconfig file %v: %v", sshConfigFile, err)
	}
	return nil
}

func generateSSHKey(keyFile string, username string, hostname string) error {
	path, err := ExecLookPath("ssh-keygen")
	if err != nil {
		return fmt.Errorf("could not find ssh-keygen on path: %v", err)
	}
	args := []string{
		"-P", "",
		"-t", "ed25519",
		"-f", keyFile,
		"-C", fmt.Sprintf("%v@%v generated by Fuchsia GN SDK", username, hostname),
	}
	cmd := ExecCommand(path, args...)
	_, err = cmd.Output()
	if err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			return fmt.Errorf("%v: %v", string(exitError.Stderr), exitError)
		}
		return err
	}
	return nil
}

func generatePublicSSHKeyfile(keyFile string, authFile string) error {
	path, err := ExecLookPath("ssh-keygen")
	if err != nil {
		return fmt.Errorf("could not find ssh-keygen on path: %v", err)
	}
	args := []string{
		"-y",
		"-f", keyFile,
	}
	cmd := ExecCommand(path, args...)
	publicKey, err := cmd.Output()
	if err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			return fmt.Errorf("%v: %v", string(exitError.Stderr), exitError)
		}
		return err
	}

	if err := os.MkdirAll(filepath.Dir(authFile), 0755); err != nil {
		return err
	}

	output, err := os.Create(authFile)
	if err != nil {
		return err
	}
	defer output.Close()

	fmt.Fprintln(output, publicKey)
	return nil
}

func writeSSHConfigFile(sshConfigFile string, versionTag string, keyFile string) error {

	if err := os.MkdirAll(filepath.Dir(sshConfigFile), 0755); err != nil {
		return err
	}

	output, err := os.Create(sshConfigFile)
	if err != nil {
		return err
	}
	defer output.Close()

	fmt.Fprintf(output, "# %s\n", versionTag)
	fmt.Fprintf(output,
		`# Configure port 8022 for connecting to a device with the local address.
# This makes it possible to forward 8022 to a device connected remotely.
# The fuchsia private key is used for the identity.
Host 127.0.0.1
	Port 8022

Host ::1
	Port 8022

Host *
# Turn off refusing to connect to hosts whose key has changed
StrictHostKeyChecking no
CheckHostIP no

# Disable recording the known hosts
UserKnownHostsFile=/dev/null

# Do not forward auth agent connection to remote, no X11
ForwardAgent no
ForwardX11 no

# Connection timeout in seconds
ConnectTimeout=10

# Check for server alive in seconds, max count before disconnecting
ServerAliveInterval 1
ServerAliveCountMax 10

# Try to keep the master connection open to speed reconnecting.
ControlMaster auto
ControlPersist yes

# When expanded, the ControlPath below cannot have more than 90 characters
# (total of 108 minus 18 used by a random suffix added by ssh).
# '%%C' expands to 40 chars and there are 9 fixed chars, so '~' can expand to
# up to 41 chars, which is a reasonable limit for a user's home in most
# situations. If '~' expands to more than 41 chars, the ssh connection
# will fail with an error like:
#     unix_listener: path "..." too long for Unix domain socket
# A possible solution is to use /tmp instead of ~, but it has
# its own security concerns.
ControlPath=~/.ssh/fx-%%C

# Connect with user, use the identity specified.
User fuchsia
IdentitiesOnly yes
IdentityFile "%v"
GSSAPIDelegateCredentials no

`, keyFile)

	return nil
}

func moveLegacyKeys(sdk SDKProperties, destAuthFile string, destKeyFile string) error {

	// Check for legacy GN SDK key and copy it to the new location.
	var (
		legacySSHDir   = filepath.Join(sdk.GetSDKDataPath(), ".ssh")
		legacyKeyFile  = filepath.Join(legacySSHDir, "pkey")
		legacyAuthFile = filepath.Join(legacySSHDir, "authorized_keys")
	)
	if FileExists(legacyKeyFile) {
		fmt.Fprintf(os.Stderr, "Migrating legacy key file %v to %v\n", legacyKeyFile, destKeyFile)
		if err := os.Rename(legacyKeyFile, destKeyFile); err != nil {
			return err
		}
		if FileExists(legacyAuthFile) {
			if err := os.Rename(legacyAuthFile, destAuthFile); err != nil {
				return err
			}
		}
	}
	return nil
}

// GetValidPropertyNames returns the list of valid properties for a
// device configuration.
func (sdk SDKProperties) GetValidPropertyNames() []string {
	return validPropertyNames[:]
}

// IsValidProperty returns true if the property is a valid
// property name.
func (sdk SDKProperties) IsValidProperty(property string) bool {
	for _, item := range validPropertyNames {
		if item == property {
			return true
		}
	}
	return false
}

// GetFuchsiaProperty returns the value for the given property for the given device.
// If the device name is empty, the default device is used via GetDefaultDevice().
// It is an error if the property cannot be found.
func (sdk SDKProperties) GetFuchsiaProperty(device string, property string) (string, error) {
	deviceConfig, err := sdk.GetDefaultDevice(device)
	if err != nil {
		return "", fmt.Errorf("Could not read configuration data for %v : %v", device, err)
	}
	if deviceConfig.DeviceName != "" {
		device = deviceConfig.DeviceName
	}
	switch property {
	case BucketKey:
		return deviceConfig.Bucket, nil
	case DeviceIPKey:
		return deviceConfig.DeviceIP, nil
	case DeviceNameKey:
		return deviceConfig.DeviceName, nil
	case ImageKey:
		return deviceConfig.Image, nil
	case PackagePortKey:
		return deviceConfig.PackagePort, nil
	case PackageRepoKey:
		return deviceConfig.PackageRepo, nil
	case SSHPortKey:
		return deviceConfig.SSHPort, nil
	}
	return "", fmt.Errorf("Could not find property %v.%v", device, property)
}

func (sdk SDKProperties) updateConfigIfDeviceIsDiscoverable(deviceConfig *DeviceConfig, discoverableDevices []*FuchsiaDevice) DeviceConfig {
	for _, discoverableDevice := range discoverableDevices {
		if deviceConfig.DeviceName == discoverableDevice.Name {
			deviceConfig.Discoverable = true
			// If DeviceIP is empty, update it from ffx target list output.
			if deviceConfig.DeviceIP == "" {
				deviceConfig.DeviceIP, deviceConfig.SSHPort = discoverableDevice.getIPAddressAndPort()
			}
			return *deviceConfig
		}
	}
	deviceConfig.Discoverable = false
	return *deviceConfig
}

func (sdk SDKProperties) mergeDeviceConfigsWithDiscoverableDevices(configs []DeviceConfig) []DeviceConfig {
	visitedDevices := map[string]bool{}
	var finalConfigs []DeviceConfig
	// Get the devices that are discoverable.
	discoverableDevices, err := sdk.listDevices()
	if err != nil {
		log.Debugf("Got an error when listing devices: %v", err)
		return configs
	}
	if len(discoverableDevices) == 0 {
		return configs
	}

	for _, config := range configs {
		if visitedDevices[config.DeviceName] {
			continue
		}
		visitedDevices[config.DeviceName] = true
		sdk.updateConfigIfDeviceIsDiscoverable(&config, discoverableDevices)
		sdk.setDeviceDefaults(&config)
		finalConfigs = append(finalConfigs, config)
	}
	for _, discoverableDevice := range discoverableDevices {
		if visitedDevices[discoverableDevice.Name] {
			continue
		}
		visitedDevices[discoverableDevice.Name] = true
		ip, port := discoverableDevice.getIPAddressAndPort()
		newConfig := DeviceConfig{
			DeviceName:   discoverableDevice.Name,
			DeviceIP:     ip,
			SSHPort:      port,
			Discoverable: true,
		}
		sdk.setDeviceDefaults(&newConfig)
		finalConfigs = append(finalConfigs, newConfig)
	}
	return finalConfigs
}

// getConfiguredDevices gets a list of devices that are configured in ffx config.
func (sdk SDKProperties) getConfiguredDevices(isMigration bool) ([]DeviceConfig, error) {
	var configs []DeviceConfig
	// Get all config data.
	configData, err := getDeviceConfigurationData(sdk, deviceConfigurationKey)
	if err != nil {
		return configs, fmt.Errorf("Could not read configuration data : %v", err)
	}

	defaultDeviceName, err := sdk.getDefaultFFXDevice()
	if err != nil {
		return configs, err
	}

	// If the default device name is "", we don't need to check if we visited it.
	visitedDefaultDevice := defaultDeviceName == ""

	if deviceConfigMap, ok := configData[deviceConfigurationKey].(map[string]interface{}); ok {
		for k, v := range deviceConfigMap {
			if !isReservedProperty(k) {
				if device, ok := sdk.mapToDeviceConfig(k, v); ok {
					if defaultDeviceName == device.DeviceName {
						device.IsDefault = true
						visitedDefaultDevice = true
					}
					sdk.setDeviceDefaults(&device)
					configs = append(configs, device)
				}
			}
		}
	}
	// If we are migrating device configurations from global to user,
	// we don't want to append the default device if it wasn't seen already.
	if isMigration {
		return configs, nil
	}
	if !visitedDefaultDevice {
		newConfig := DeviceConfig{
			DeviceName: defaultDeviceName,
			IsDefault:  true,
		}
		sdk.setDeviceDefaults(&newConfig)
		configs = append(configs, newConfig)
	}
	return configs, nil
}

// GetDeviceConfigurations returns a list of all device configurations.
func (sdk SDKProperties) GetDeviceConfigurations() ([]DeviceConfig, error) {
	configs, err := sdk.getConfiguredDevices(false)
	if err != nil {
		return nil, err
	}
	return sdk.mergeDeviceConfigsWithDiscoverableDevices(configs), nil
}

// GetDeviceConfiguration returns the configuration for the device with the given name.
func (sdk SDKProperties) GetDeviceConfiguration(name string) (DeviceConfig, error) {
	var deviceConfig DeviceConfig

	dataKey := getDeviceDataKey([]string{name}, false)
	configData, err := getDeviceConfigurationData(sdk, dataKey)
	if err != nil {
		return deviceConfig, fmt.Errorf("Could not read configuration data : %v", err)
	}
	if len(configData) == 0 {
		deviceConfig = DeviceConfig{
			DeviceName: name,
		}
		sdk.setDeviceDefaults(&deviceConfig)
		return deviceConfig, nil
	}

	if deviceData, ok := configData[dataKey]; ok {
		if deviceConfig, ok := sdk.mapToDeviceConfig(name, deviceData); ok {
			defaultDeviceName, err := sdk.getDefaultFFXDevice()
			if err != nil {
				return deviceConfig, err
			}
			deviceConfig.IsDefault = deviceConfig.DeviceName == defaultDeviceName
			// Set the default values for the  device, even if not set explicitly
			// This centralizes the configuration into 1 place.
			sdk.setDeviceDefaults(&deviceConfig)
			return deviceConfig, nil
		}
		return deviceConfig, fmt.Errorf("Cannot parse DeviceConfig from %v", configData)
	}
	return deviceConfig, fmt.Errorf("Cannot parse DeviceData.%v from %v", name, configData)
}

// SetDeviceIP manually adds a target via `ffx target add`.
func (sdk SDKProperties) SetDeviceIP(deviceIP, sshPort string) error {
	if sshPort == "" {
		sshPort = DefaultSSHPort
	}
	fullAddr := net.JoinHostPort(deviceIP, sshPort)
	ffxTargetAddArgs := []string{"target", "add", fullAddr}
	log.Debugf("Adding target using ffx %s", ffxTargetAddArgs)
	if _, err := sdk.RunFFX(ffxTargetAddArgs, false); err != nil {
		return fmt.Errorf("unable to add target via ffx %s: %w", ffxTargetAddArgs, err)
	}
	return nil
}

// SaveDeviceConfiguration persists the given device configuration properties.
func (sdk SDKProperties) SaveDeviceConfiguration(newConfig DeviceConfig) error {
	// Create a map of key to value to store. Only write out values that are explicitly set to something
	// that is not the default.
	origConfig, err := sdk.GetDeviceConfiguration(newConfig.DeviceName)
	if err != nil {
		return err
	}
	defaultConfig := DeviceConfig{DeviceName: newConfig.DeviceName}
	sdk.setDeviceDefaults(&defaultConfig)

	dataMap := make(map[string]string)
	// If the value changed from the original, write it out. We only write configurations
	// to ffx if they are not the default unless a value was previously written.
	if origConfig.Bucket != newConfig.Bucket {
		dataMap[getDeviceDataKey([]string{newConfig.DeviceName, BucketKey}, false)] = newConfig.Bucket
	}
	if origConfig.Image != newConfig.Image {
		dataMap[getDeviceDataKey([]string{newConfig.DeviceName, ImageKey}, false)] = newConfig.Image
	}
	if origConfig.PackagePort != newConfig.PackagePort {
		dataMap[getDeviceDataKey([]string{newConfig.DeviceName, PackagePortKey}, false)] = newConfig.PackagePort
	}
	if origConfig.PackageRepo != newConfig.PackageRepo {
		dataMap[getDeviceDataKey([]string{newConfig.DeviceName, PackageRepoKey}, false)] = newConfig.PackageRepo
	}
	if newConfig.IsDefault {
		if err := sdk.setFFXDefaultDevice(newConfig.DeviceName); err != nil {
			return fmt.Errorf("unable to set default device via ffx: %v", err)
		}
	}

	for key, value := range dataMap {
		if err := writeConfigurationData(sdk, key, value); err != nil {
			return err
		}
	}
	return nil
}

// setFFXDefaultDevice sets the default device in ffx.
func (sdk SDKProperties) setFFXDefaultDevice(deviceName string) error {
	args := []string{"target", "default", "set", deviceName}
	log.Debugf("Setting default device via ffx: %v\n", args)
	_, err := sdk.RunFFX(args, false)
	return err
}

// unsetFFXDefaultDevice unsets the default device in ffx.
func (sdk SDKProperties) unsetFFXDefaultDevice() error {
	args := []string{"target", "default", "unset"}
	log.Debugf("Unsetting default device via ffx: %v\n", args)
	_, err := sdk.RunFFX(args, false)
	return err
}

// ResolveTargetAddress evaluates the deviceIP and deviceName passed in
// to determine the target IP address. This include consulting the configuration
// information set via `ffx`.
func (sdk SDKProperties) ResolveTargetAddress(deviceIP string, deviceName string) (DeviceConfig, error) {
	var (
		targetAddress string
		err           error
	)

	// If there is a deviceIP address, use it.
	if deviceIP != "" {
		defaultConfig := DeviceConfig{
			DeviceIP: deviceIP,
		}
		sdk.setDeviceDefaults(&defaultConfig)
		return defaultConfig, nil
	}

	config, err := sdk.GetDefaultDevice(deviceName)
	if err != nil {
		return DeviceConfig{}, err
	}

	targetAddress = config.DeviceIP
	if config.DeviceName != "" {
		deviceName = config.DeviceName
	}

	if deviceName == "" && targetAddress == "" {
		return DeviceConfig{}, fmt.Errorf("No devices found. %v", helpfulTipMsg)
	}

	if targetAddress == "" {
		return DeviceConfig{}, fmt.Errorf(`Cannot get target address for %v.
		Try running 'ffx target list'.`, deviceName)
	}

	return config, nil
}

// GetDefaultDevice gets the default device to use by default.
func (sdk SDKProperties) GetDefaultDevice(deviceName string) (DeviceConfig, error) {
	if err := sdk.MigrateGlobalData(); err != nil {
		return DeviceConfig{}, err
	}

	configs, err := sdk.GetDeviceConfigurations()
	if err != nil {
		return DeviceConfig{}, err
	}

	if deviceName != "" {
		for _, config := range configs {
			if config.DeviceName == deviceName {
				return config, nil
			}
		}
		return sdk.setDeviceDefaults(&DeviceConfig{
			DeviceName: deviceName,
		}), nil
	}

	var discoverableDevicesConfigs []DeviceConfig
	// Check if there is a default device configured, if there is use it.
	for _, config := range configs {
		if config.IsDefault {
			return config, nil
		}
		if config.Discoverable {
			discoverableDevicesConfigs = append(discoverableDevicesConfigs, config)
		}
	}

	if len(discoverableDevicesConfigs) == 0 {
		defaultConfig := DeviceConfig{}
		sdk.setDeviceDefaults(&defaultConfig)
		return defaultConfig, nil
	}

	if len(discoverableDevicesConfigs) > 1 {
		return DeviceConfig{}, fmt.Errorf("Multiple devices found. %v", helpfulTipMsg)
	}
	return discoverableDevicesConfigs[0], nil
}

// writeConfigurationData calls `ffx` to store the value at the specified key.
func writeConfigurationData(sdk SDKProperties, key string, value string) error {
	args := []string{"config", "set", key, value}
	if _, err := sdk.RunFFX(args, false); err != nil {
		return fmt.Errorf("Error writing %s = %s: %w", key, value, err)
	}
	return nil
}

// isDeviceInList checks if device name is in the list.
func isDeviceInList(devices []DeviceConfig, deviceName string) bool {
	for _, device := range devices {
		if device.DeviceName == deviceName {
			return true
		}
	}
	return false
}

// MigrateGlobalData migrates global DeviceConfiguration to user level using
// device_config key.
func (sdk SDKProperties) MigrateGlobalData() error {
	// Get all global config data in DeviceConfiguration. This doesn't look
	// at devices that are discoverable by ffx target list.
	configData, err := getDeviceConfigurationData(sdk, globalDeviceConfigurationKey)
	if err != nil {
		return fmt.Errorf("could not read global configuration data: %w", err)
	}
	// If there is no global configuration data, simply return.
	if configData == nil {
		return nil
	}

	// Get the list of already configured device in user level. We don't want to
	// migrate devices that are already configured in user level.
	// This doesn't look at devices that are discoverable by ffx target list.
	alreadyConfiguredDevices, err := sdk.getConfiguredDevices(true)
	if err != nil {
		return err
	}

	if deviceConfigMap, ok := configData[globalDeviceConfigurationKey].(map[string]interface{}); ok {
		for k, v := range deviceConfigMap {
			if !isReservedProperty(k) {
				if device, ok := sdk.mapToDeviceConfig(k, v); ok {
					if isDeviceInList(alreadyConfiguredDevices, device.DeviceName) {
						continue
					}
					// Save the device to user configuration. We purposely don't remove the device
					// in order to ensure that different versions of the tools still work and don't
					// require reconfiguration.
					if err := sdk.SaveDeviceConfiguration(device); err != nil {
						return fmt.Errorf("failed to migrate ffx device configurations from global to user: %w", err)
					}
				}
			}
		}
	}
	return nil
}

// getDeviceConfigurationData calls `ffx` to read the data at the specified key.
func getDeviceConfigurationData(sdk SDKProperties, key string) (map[string]interface{}, error) {
	var (
		data   map[string]interface{}
		err    error
		output string
	)

	args := []string{"config", "get", key}

	if output, err = sdk.RunFFX(args, false); err != nil {
		// Exit code of 2 means no value was found.
		if exiterr, ok := err.(*exec.ExitError); ok && exiterr.ExitCode() == 2 {
			return data, nil
		}
		return data, fmt.Errorf("Error reading %v: %v %v", key, err, output)
	}

	if len(output) > 0 {
		jsonString := string(output)

		// wrap the response in {} and double quote the key so it is suitable for json unmarshaling.
		fullJSONString := "{\"" + key + "\": " + jsonString + "}"
		err := json.Unmarshal([]byte(fullJSONString), &data)
		if err != nil {
			return data, fmt.Errorf("Error parsing configuration data %v: %s", err, fullJSONString)
		}
	}
	return data, nil
}

// RunFFX executes ffx with the given args, returning stdout. If there is an error,
// the error will usually be of type *ExitError.
func (sdk SDKProperties) RunFFX(args []string, interactive bool) (string, error) {
	toolsDir, err := sdk.GetToolsDir()
	if err != nil {
		return "", fmt.Errorf("Could not determine tools directory %v", err)
	}
	cmd := filepath.Join(toolsDir, "ffx")

	// If run in infra, check that ffx is isolated.
	if os.Getenv(constants.TestOutDirEnvKey) != "" && os.Getenv(ffxutil.FFXIsolateDirEnvKey) == "" {
		return "", fmt.Errorf("ffx must be isolated when run in infra")
	}

	ffx := ExecCommand(cmd, args...)
	if interactive {
		ffx.Stderr = os.Stderr
		ffx.Stdout = os.Stdout
		ffx.Stdin = os.Stdin
		return "", ffx.Run()
	}
	output, err := ffx.Output()
	if err != nil {
		return "", err
	}
	return string(output), err
}

// isReservedProperty used to differentiate between properties used
// internally and device names.
func isReservedProperty(property string) bool {
	switch property {
	case defaultDeviceKey:
		return true
	}
	return false
}

// mapToDeviceConfig converts the map returned by json into a DeviceConfig struct.
func (sdk SDKProperties) mapToDeviceConfig(deviceName string, data interface{}) (DeviceConfig, bool) {
	var (
		device     DeviceConfig
		deviceData map[string]interface{}
		ok         bool
		value      string
	)

	device.DeviceName = deviceName

	if deviceData, ok = data.(map[string]interface{}); ok {
		for _, key := range validPropertyNames {
			// The Default flag, IP address, and SSH port are stored else where, so don't
			// try to key it from the map.
			if key == DefaultKey || key == DeviceIPKey || key == SSHPortKey {
				continue
			}
			// Use Sprintf to convert the value into a string.
			// This is done since some values are numeric and are
			// not unmarshalled as strings.
			if val, ok := deviceData[key]; ok {
				value = fmt.Sprintf("%v", val)
			} else {
				// Setting the value to empty string makes it that the device default
				// value is used instead.
				value = ""
			}
			switch key {
			case BucketKey:
				device.Bucket = value
			case ImageKey:
				device.Image = value
			case PackagePortKey:
				device.PackagePort = value
			case PackageRepoKey:
				device.PackageRepo = value
			}
		}
	}
	return sdk.setDeviceDefaults(&device), ok
}
