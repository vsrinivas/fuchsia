// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sdkcommon

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
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

// Default GCS bucket for prebuilt images and packages.
const defaultGCSbucket string = "fuchsia"

// GCSImage is used to return the bucket, name and version of a prebuilt.
type GCSImage struct {
	Bucket  string
	Name    string
	Version string
}

// SDKProperties holds the common data for SDK tools.
// These values should be set or initialized by calling
// (sdk *SDKProperties) Init().
type SDKProperties struct {
	DataPath string
	Version  string
}

// DefaultGetUserHomeDir is the default implmentation of GetUserHomeDir()
// to allow mocking of user.Current()
func DefaultGetUserHomeDir() (string, error) {
	usr, err := user.Current()
	if err != nil {
		return "", nil
	}
	return usr.HomeDir, nil
}

// DefaultGetUsername is the default implmentation of GetUsername()
// to allow mocking of user.Current()
func DefaultGetUsername() (string, error) {
	usr, err := user.Current()
	if err != nil {
		return "", nil
	}
	return usr.Username, nil
}

// DefaultGetHostname is the default implmentation of GetHostname()
// to allow mocking of user.Current()
func DefaultGetHostname() (string, error) {
	return os.Hostname()
}

// GetUserHomeDir allow mocking
var GetUserHomeDir = DefaultGetUserHomeDir
var GetUsername = DefaultGetUsername
var GetHostname = DefaultGetHostname

// Init initializes the SDK properties.
// TODO(fxb/64107): Refactor this to New()
func (sdk *SDKProperties) Init() error {
	homeDir, err := GetUserHomeDir()
	if err != nil {
		return err
	}
	sdk.DataPath = filepath.Join(homeDir, ".fuchsia")
	toolsDir, err := sdk.GetToolsDir()
	if err != nil {
		return err
	}
	manifestFile, err := filepath.Abs(filepath.Join(toolsDir, "..", "..", "meta", "manifest.json"))
	if err != nil {
		return err
	}
	// If this is running in-tree, the manifest may not exist.
	if FileExists(manifestFile) {
		if sdk.Version, err = getSDKVersion(manifestFile); err != nil {
			return err
		}
	} else {
		log.Warningf("Cannot find SDK manifest file %v", manifestFile)
	}
	return nil
}

// getSDKVersion reads the manifest JSON file and returns the "id" property.
func getSDKVersion(manifestFilePath string) (string, error) {
	manifestFile, err := os.Open(manifestFilePath)
	// if we os.Open returns an error then handle it
	if err != nil {
		return "", err
	}
	defer manifestFile.Close()
	data, err := ioutil.ReadAll(manifestFile)
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
// This is the default package repository path, thinking there will
// be other repositories in the future.
func (sdk SDKProperties) GetDefaultPackageRepoDir() (string, error) {
	return filepath.Join(sdk.DataPath, "packages", "amber-files"), nil
}

// GetDefaultGCSBucket returns the default GCS bucket name.
func (sdk SDKProperties) GetDefaultGCSBucket() (string, error) {
	return "fuchsia", nil
}

// GetDefaultGCSImage returns the default GCS image name.
func (sdk SDKProperties) GetDefaultGCSImage() (string, error) {
	return "", nil
}

// GetDefaultPackageServerPort returns the TCP port the package server should use.
func (sdk SDKProperties) GetDefaultPackageServerPort() (string, error) {
	return "8083", nil
}

// GetDefaultDeviceName returns the default target device name.
func (sdk SDKProperties) GetDefaultDeviceName() (string, error) {
	return "", nil
}

// GetDefaultDeviceIPAddress returns the default target device IP address.
func (sdk SDKProperties) GetDefaultDeviceIPAddress() (string, error) {
	return "", nil
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
		return "", fmt.Errorf("could not get directory of currently running file: %s", err)
	}
	return dir, nil
}

// GetAvailableImages returns the images available for the given version and bucket. If
// bucket is not the default bucket, the images in the default bucket are also returned.
func (sdk SDKProperties) GetAvailableImages(version string, bucket string) ([]GCSImage, error) {
	var buckets []string
	var images []GCSImage

	if bucket == "" || bucket == defaultGCSbucket {
		buckets = []string{defaultGCSbucket}
	} else {
		buckets = []string{bucket, defaultGCSbucket}
	}

	for _, b := range buckets {
		url := fmt.Sprintf("gs://%v/development/%v/images", b, version)
		args := []string{"ls", url}
		output, err := runGSUtil(args)
		if err != nil {
			return images, err
		}
		for _, line := range strings.Split(strings.TrimSuffix(string(output), "\n"), "\n") {
			if len(filepath.Base(line)) >= 4 {
				name := filepath.Base(line)[:len(filepath.Base(line))-4]
				images = append(images, GCSImage{Bucket: b, Version: version, Name: name})
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

//GetAddressByName returns the IPv6 address of the device.
func (sdk SDKProperties) GetAddressByName(deviceName string) (string, error) {
	toolsDir, err := sdk.GetToolsDir()
	if err != nil {
		return "", fmt.Errorf("Could not determine tools directory %v", err)
	}
	cmd := filepath.Join(toolsDir, "device-finder")

	args := []string{"resolve", "-device-limit", "1", "-ipv4=false", deviceName}

	output, err := ExecCommand(cmd, args...).Output()
	if err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			return "", fmt.Errorf("%v: %v", string(exitError.Stderr), exitError)
		} else {
			return "", err
		}
	}
	return string(output), nil
}

// RunSSHCommand runs the command provided in args on the given target device.
// The customSSHconfig is optional and overrides the SSH configuration defined by the SDK.
// privateKey is optional to specify a private key to use to access the device.
func (sdk SDKProperties) RunSSHCommand(targetAddress string, customSSHConfig string, privateKey string, args []string) (string, error) {

	if customSSHConfig == "" || privateKey == "" {
		if err := checkSSHConfig(sdk); err != nil {
			return "", err
		}
	}

	var cmdArgs []string
	if customSSHConfig != "" {
		cmdArgs = append(cmdArgs, "-F", customSSHConfig)
	} else {
		cmdArgs = []string{"-F", getFuchsiaSSHConfigFile(sdk)}
	}
	if privateKey != "" {
		cmdArgs = append(cmdArgs, "-i", privateKey)
	}

	cmdArgs = append(cmdArgs, targetAddress)

	cmdArgs = append(cmdArgs, args...)

	return runSSH(cmdArgs)
}

func getFuchsiaSSHConfigFile(sdk SDKProperties) string {
	return filepath.Join(sdk.DataPath, "sshconfig")
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
		config, err := ioutil.ReadFile(sshConfigFile)
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
				return fmt.Errorf("Could generate private SSH key: %v", err)
			}
		}
		if err := generatePublicSSHKeyfile(keyFile, authFile); err != nil {
			return fmt.Errorf("Could get public keys from private SSH key: %v", err)
		}
	}

	if err := writeSSHConfigFile(sshConfigFile, sshConfigTag, keyFile); err != nil {
		return fmt.Errorf("Could write sshconfig file %v: %v", sshConfigFile, err)
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
		} else {
			return err
		}
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
		} else {
			return err
		}
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
		legacySSHDir   = filepath.Join(sdk.DataPath, ".ssh")
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
