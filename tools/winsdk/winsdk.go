// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a tool to pack a minimal Windows SDK package that allows clang to compile or
// cross compile a binary targeting Windows. Before running this tool, please make sure
// you have Visual Studio or Visual Studio BuildTools and Windows SDK installed. The
// tool was tested with following
// installation procedures:
//
// - Download Visual Studio or Visual Studio BuildTools installer
// - Install Visual Studio Build Tools and Windows SDK.
//
// To install the necessary components, you can use the following command:
//
//	 .\vs_BuildTools.exe --passive ^
//	    --add Microsoft.VisualStudio.Component.VC.CoreBuildTools ^
//		--add Microsoft.VisualStudio.Component.VC.CoreIde ^
//		--add Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core ^
//		--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
//		--add Microsoft.VisualStudio.Component.VC.Tools.ARM64 ^
//		--add Microsoft.VisualStudio.Component.Windows10SDK ^
//		--add Microsoft.VisualStudio.Component.Windows10SDK.19041 ^
//		--add Microsoft.VisualStudio.Component.VC.Redist.14.Latest ^
//		--add Microsoft.VisualStudio.Component.VC.ATLMFC ^
//
// To make this go script runnable from anywhere, please avoid adding third-party
// dependencies.
package main

import (
	"archive/zip"
	"bufio"
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"hash"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"text/template"
	"time"
)

const (
	// VC Runtime versions from VS2022 and 2019 are started with 14.
	// NOTE: this value may need to be updated in the future.
	VcrtVersionWildcard = "14.*.*"
	// When using a newer WinSDK which primarily targets Windows 11,
	// this number will be 11.
	WinKitVersion     = "10"
	DefaultSDKVersion = "10.0.19041.0"
	DefaultVSVersion  = "2022"
)

var (
	sdkVersion  string
	dryRun      bool
	outputPath  string
	vsVersion   string
	archivePath string
	help        bool
)

func init() {
	flag.StringVar(&sdkVersion, "sdkversion", DefaultSDKVersion, fmt.Sprintf("Windows SDK version, such as \"%s\"", DefaultSDKVersion))
	flag.BoolVar(&dryRun, "dryrun", false, "scan for file existence and prints statistics")
	flag.StringVar(&outputPath, "output", "", "output directory, such as \"winsdk\"")
	flag.StringVar(&vsVersion, "vs", DefaultVSVersion, fmt.Sprintf("Visual Studio version, such as \"%s\"", DefaultVSVersion))
	flag.StringVar(&archivePath, "archive", "", "generate the SDK package as an archive instead of a package, cannot be used when \"output\" is already defined")
	flag.BoolVar(&help, "help", false, "show help information")
}

type Lock struct {
	Filename string `json:"filename"`
	Hash     string `json:"hash"`
}

type Locks []Lock

func (l Locks) Len() int {
	return len(l)
}

func (l Locks) Swap(i, j int) {
	l[i], l[j] = l[j], l[i]
}

func (l Locks) Less(i, j int) bool {
	return l[i].Filename < l[j].Filename
}

// The lock file generated from this script saves the time stamp, hash for the
// entire SDK archive and hashes of the content of the top level directories
// inside the SDK archive. Example of the content of a lock file:
// {
//
//	"updated": "2022-06-24T15:55:38.3696424-07:00",
//	"hash": "daf5d8e93d54333f0b8690bf38bd4316eb4f81076c227e7b9dac163dae7ebe09",
//	"files": [
//	  {
//	    "filename": "VC",
//	    "hash": "95bee54f7804be006e19c899c8c04034cd13c4ca98bf53078055534a748655ca"
//	  },
//	  {
//	    "filename": "Windows Kits",
//	    "hash": "2a32f7edbe257fe69c6f9fb7347bd58c6277180090c3e1381e236fac1a77afdf"
//	  },
//	  {
//	    "filename": "bin",
//	    "hash": "f316714a5dd38e09b815cfed682224fc62b0fc701dac88c268fb2b463412ccbc"
//	  },
//	  {
//	    "filename": "redist",
//	    "hash": "cff8e1746dc357fe6c732bfe05b881b2df97d215e77f3425b8c142e964d7b72f"
//	  },
//	  {
//	    "filename": "sysarm64",
//	    "hash": "424a42f754b254da10793b60d289eaa2cbcd77dc2e8a2ec62601200ee5cc4416"
//	  }
//	]
//
// }
type LockFile struct {
	Updated time.Time `json:"updated"`
	Hash    string    `json:"hash"`
	Files   Locks     `json:"files"`
}

type lockFileCreator struct {
	lockfile *LockFile
	hashFunc map[string]hash.Hash
}

type packedFile struct {
	origin string
	target string
}

func newLockFileCreator() *lockFileCreator {
	return &lockFileCreator{
		lockfile: &LockFile{
			Updated: time.Now(),
			Files:   make(Locks, 0),
		},
		hashFunc: make(map[string]hash.Hash),
	}
}

func (c *lockFileCreator) addFile(filename packedFile, file *os.File) error {
	pathList := strings.Split(filename.target, string(filepath.Separator))
	if len(pathList) == 0 {
		return fmt.Errorf("target filename should not be empty: %s", filename.origin)
	}
	topLevelDir := pathList[0]
	if _, ok := c.hashFunc[topLevelDir]; !ok {
		c.hashFunc[topLevelDir] = sha256.New()
	}
	file.Seek(0, 0)
	if _, err := io.Copy(c.hashFunc[topLevelDir], file); err != nil {
		return err
	}
	return nil
}

func (c *lockFileCreator) generateLockFile() ([]byte, error) {
	for entry, val := range c.hashFunc {
		hashValue := fmt.Sprintf("%x", val.Sum(nil))
		c.lockfile.Files = append(c.lockfile.Files, Lock{Filename: entry, Hash: hashValue})
	}
	h := sha256.New()
	sort.Sort(c.lockfile.Files)
	for _, entry := range c.lockfile.Files {
		// Convert to binary data.
		data, err := hex.DecodeString(entry.Hash)
		if err != nil {
			return nil, err
		}
		h.Write(data)
	}
	c.lockfile.Hash = fmt.Sprintf("%x", h.Sum(nil))
	return json.MarshalIndent(c.lockfile, "", "  ")
}

func getVSPath() (string, error) {
	const (
		// Path to vswhere.exe is fixed regardless of bitwise of the OS on x86 or x64.
		// NOTE: With arm64 buildtools coming out, it might change in the future.
		vswherePath     = `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe`
		vsPathMarker    = "installationPath: "
		vsVersionMarker = "catalog_productLineVersion: "
	)
	cmd := exec.Command(vswherePath, "-prerelease")
	var stdoutBuf, stderrBuf bytes.Buffer
	cmd.Stdout = &stdoutBuf
	cmd.Stderr = &stderrBuf
	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf("vswhere.exe failed with %s, %v", stderrBuf.String(), err)
	}
	scanner := bufio.NewScanner(&stdoutBuf)
	var installationPath string
	var matchingVsPath string
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, vsPathMarker) {
			installationPath = line[len(vsPathMarker):]
		}
		if strings.HasPrefix(line, vsVersionMarker) {
			if line[len(vsVersionMarker):] == vsVersion {
				matchingVsPath = installationPath
			}
		}
	}
	if len(matchingVsPath) > 0 {
		return matchingVsPath, nil
	}
	// If MSVC and Windows SDK were installed through VS BuildTools installer,
	// vswhere.exe will not return the paths of them. In this case, by default,
	// the VSPath will points to
	// "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools".
	// Use the hard coded path for now until we have a better option.
	BuildToolsDir := `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`
	if info, err := os.Stat(BuildToolsDir); err == nil && info.IsDir() {
		return BuildToolsDir, nil
	}

	return "", fmt.Errorf("no matching VSPath was found")
}

func expandWildcards(root, subDir string) (string, error) {
	normPath := filepath.Clean(filepath.Join(root, subDir))
	matches, err := filepath.Glob(normPath)
	if err != nil {
		return "", err
	}
	if len(matches) != 1 {
		return "", fmt.Errorf("%s had %d matches, should be only one", normPath, len(matches))
	}
	return matches[0], nil
}

func buildFileList(vsPath, vcToolsPath, vcrtVersion string) ([]packedFile, error) {
	result := make([]packedFile, 0)
	// Subset of VS corresponding to VC.
	candidatePaths := []packedFile{
		{`DIA SDK\bin`, ""},
		{`DIA SDK\idl`, ""},
		{`DIA SDK\include`, ""},
		{`DIA SDK\lib`, ""},
		// VC Tools and link time libraries, which are largest components
		// in the SDK package.
		{filepath.Join(vcToolsPath, "crt"), ""},
		{filepath.Join(vcToolsPath, "bin"), ""},
		{filepath.Join(vcToolsPath, "include"), ""},
		{filepath.Join(vcToolsPath, "atlmfc"), ""},
		// "onecore" lib will be removed later.
		{filepath.Join(vcToolsPath, "lib"), ""},
		// VC Runtime x64.
		{`VC\redist`, ""},
		{fmt.Sprintf(`VC\redist\MSVC\%s\x86\Microsoft.VC*.CRT`, vcrtVersion), "sys32"},
		{fmt.Sprintf(`VC\redist\MSVC\%s\x86\Microsoft.VC*.CRT`, vcrtVersion), `Windows Kits\10\bin\x86`},
		{fmt.Sprintf(`VC\redist\MSVC\%s\debug_nonredist\x86\Microsoft.VC*.DebugCRT`, vcrtVersion), "sys32"},
		{fmt.Sprintf(`VC\redist\MSVC\%s\x64\Microsoft.VC*.CRT`, vcrtVersion), "sys64"},
		{fmt.Sprintf(`VC\redist\MSVC\%s\x64\Microsoft.VC*.CRT`, vcrtVersion), `VC\bin\amd64_x86`},
		{fmt.Sprintf(`VC\redist\MSVC\%s\x64\Microsoft.VC*.CRT`, vcrtVersion), `VC\bin\amd64`},
		{fmt.Sprintf(`VC\redist\MSVC\%s\x64\Microsoft.VC*.CRT`, vcrtVersion), `Windows Kits\10\bin\x64`},
		{fmt.Sprintf(`VC\redist\MSVC\%s\debug_nonredist\x64\Microsoft.VC*.DebugCRT`, vcrtVersion), "sys64"},
		// VC Runtime ARM64.
		{fmt.Sprintf(`VC\redist\MSVC\%s\arm64\Microsoft.VC*.CRT`, vcrtVersion), "sysarm64"},
		{fmt.Sprintf(`VC\redist\MSVC\%s\arm64\Microsoft.VC*.CRT`, vcrtVersion), `VC\bin\amd64_arm64`},
		{fmt.Sprintf(`VC\redist\MSVC\%s\arm64\Microsoft.VC*.CRT`, vcrtVersion), `VC\bin\arm64`},
		{fmt.Sprintf(`VC\redist\MSVC\%s\arm64\Microsoft.VC*.CRT`, vcrtVersion), `Windows Kits\10\bin\arm64`},
		{fmt.Sprintf(`VC\redist\MSVC\%s\debug_nonredist\arm64\Microsoft.VC*.DebugCRT`, vcrtVersion), "sysarm64"},
	}

	appendToResult := func(resultSlice []packedFile, items ...packedFile) []packedFile {
		for _, item := range items {
			// Skip .msi and .msm files because we don't need installers and samples.
			if strings.HasSuffix(item.origin, ".msi") || strings.HasSuffix(item.origin, ".msm") {
				continue
			}
			if filepath.Base(item.origin) == "vctip.exe" {
				// vctip.exe doesn't shutdown, leaving locks on directories. It's
				// optional so let's avoid this problem by not packaging it.
				// See https://crbug.com/735226 for more details.
				continue
			}
			// Special case for onecore vcrt. We don't use it for linking
			// so we skip it to save space.
			originList := strings.Split(item.origin, string(filepath.Separator))
			onecoreDetected := false
			for _, pathElement := range originList {
				if strings.ToLower(pathElement) == "onecore" {
					onecoreDetected = true
					break
				}
			}
			if onecoreDetected {
				continue
			}

			resultSlice = append(resultSlice, item)
		}
		return resultSlice
	}
	for _, candidatePath := range candidatePaths {
		combinedPath, err := expandWildcards(vsPath, candidatePath.origin)
		if err != nil {
			return result, err
		}
		if info, err := os.Stat(combinedPath); os.IsNotExist(err) || !info.IsDir() {
			if os.IsNotExist(err) {
				return result, fmt.Errorf("%s missing", combinedPath)
			}
			if !info.IsDir() {
				return result, fmt.Errorf("%s is not a directory", combinedPath)
			}
		}
		if err := filepath.Walk(combinedPath, func(path string, info fs.FileInfo, err error) error {
			if err != nil {
				return err
			}
			// Skip the file if it is a directory.
			if info.IsDir() {
				return nil
			}
			if len(candidatePath.target) != 0 {
				// Target directory in the SDK archive is explicitly defined.
				// Calculate the relative path and save the info.
				if !strings.HasPrefix(path, combinedPath) {
					panic(fmt.Sprintf("%s should be in subdirectory of %s", path, combinedPath))
				}
				dest := path[len(combinedPath)+1:]
				result = appendToResult(result, packedFile{path, filepath.Clean(filepath.Join(candidatePath.target, dest))})
			} else {
				// Target directory in the SDK archive is implicit. Use its relative path
				// to Visual Studio installation path as its target path.
				if !strings.HasPrefix(path, vsPath) {
					panic(fmt.Sprintf("%s should be in subdirectory of %s", path, vsPath))
				}
				dest := path[len(vsPath)+1:]
				result = appendToResult(result, packedFile{path, dest})
			}
			return nil
		}); err != nil {
			return result, err
		}
	}
	// Read reg table to locate Windows SDK path.
	regCommand := exec.Command("reg", "query", `HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots`, "/v", "KitsRoot10")
	var stdoutBuf, stderrBuf bytes.Buffer
	regCommand.Stdout = &stdoutBuf
	regCommand.Stderr = &stderrBuf
	if err := regCommand.Run(); err != nil {
		return result, fmt.Errorf("command failed with msg: %s and %v", stderrBuf.String(), err)
	}
	scanner := bufio.NewScanner(&stdoutBuf)
	const marker = "    KitsRoot10    REG_SZ    "
	sdkPath := ""
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, marker) {
			sdkPath = line[len(marker):]
		}
	}
	if sdkPath == "" {
		return result, fmt.Errorf("Windows SDK path cannot be found")
	}
	if strings.HasSuffix(sdkPath, string(filepath.Separator)) {
		sdkPath = sdkPath[:len(sdkPath)-1]
	}

	sdkDirList := []string{
		// Skip debugger(windbg) since we don't use it for building anything.
		`References\`,
		`Windows Performance Toolkit\`,
		`Testing\`,
		`App Certification Kit\`,
		`Extension SDKs\`,
		`Assessment and Deployment Kit\`,
	}
	if err := filepath.Walk(sdkPath, func(path string, info fs.FileInfo, err error) error {
		if err != nil {
			return err
		}
		// Skip the file if it is a directory.
		if info.IsDir() {
			return nil
		}

		combinedPath := filepath.Clean(path)
		// Skip files we don't need. These files may also be very long (and exceed _MAX_PATH).
		tailPath := combinedPath[len(sdkPath)+1:]
		for _, dir := range sdkDirList {
			if strings.HasPrefix(tailPath, dir) {
				return nil
			}
		}
		// Skip Include and Library files that are not matching to the supplied SDK version.
		if strings.HasPrefix(tailPath, `Include\`) || strings.HasPrefix(tailPath, `Lib\`) || strings.HasPrefix(tailPath, `Source\`) || strings.HasPrefix(tailPath, `bin\`) {
			if !strings.Contains(tailPath, sdkVersion) {
				return nil
			}
		}
		destPath := filepath.Join("Windows Kits", WinKitVersion, tailPath)
		result = appendToResult(result, packedFile{combinedPath, destPath})
		return nil
	}); err != nil {
		return result, err
	}
	// Copy ucrt DLLs.
	addUCRTFiles := func(sdkPath, arch string) ([]packedFile, error) {
		ucrtDir := filepath.Join(sdkPath, "redist", sdkVersion, "ucrt", "dlls", arch)
		if _, err := os.Stat(ucrtDir); os.IsNotExist(err) {
			ucrtDir = filepath.Join(sdkPath, "redist", "ucrt", "dlls", arch)
		}
		ucrtPaths, err := filepath.Glob(filepath.Join(ucrtDir, "*"))
		if err != nil {
			return nil, err
		}
		if len(ucrtPaths) == 0 {
			return nil, fmt.Errorf("%s is emptry, ucrt dlls could not be located", ucrtDir)
		}
		tmpResult := make([]packedFile, 0)
		for _, ucrtPath := range ucrtPaths {
			// Use a different implementation than the chromium packer.
			dest := ucrtPath[len(sdkPath)+1:]
			tmpResult = append(tmpResult, packedFile{ucrtPath, dest})
		}
		return tmpResult, nil
	}
	ucrtX64Files, err := addUCRTFiles(sdkPath, "x64")
	if err != nil {
		return result, err
	}
	result = appendToResult(result, ucrtX64Files...)
	ucrtARMFiles, err := addUCRTFiles(sdkPath, "arm")
	if err != nil {
		return result, err
	}
	result = appendToResult(result, ucrtARMFiles...)
	systemCRTFiles := []string{
		"ucrtbased.dll",
	}
	archList := []string{
		"x86",
		"x64",
		"arm64",
	}
	for _, systemCRTFile := range systemCRTFiles {
		for _, arch := range archList {
			srcPath := filepath.Join(sdkPath, "bin", sdkVersion, arch, "ucrt", systemCRTFile)
			destPath := srcPath[len(sdkPath)+1:]
			result = appendToResult(result, packedFile{srcPath, destPath})
		}
	}
	return result, nil
}

func addEnvSetup(files *[]packedFile, vcToolsPath string) (string, error) {
	tmpDir, err := os.MkdirTemp(os.TempDir(), "winsdk*")
	if err != nil {
		return "", err
	}
	vcToolsParts := strings.Split(vcToolsPath, string(filepath.Separator))

	includeDirs := [][]string{
		{"Windows Kits", WinKitVersion, "Include", sdkVersion, "um"},
		{"Windows Kits", WinKitVersion, "Include", sdkVersion, "shared"},
		{"Windows Kits", WinKitVersion, "Include", sdkVersion, "winrt"},
		{"Windows Kits", WinKitVersion, "Include", sdkVersion, "ucrt"},
		append(vcToolsParts, "include"),
		append(vcToolsParts, "atlmfc", "include"),
	}

	libPathDirs := [][]string{
		append(vcToolsParts, "lib", "x86", "store", "reference"),
		{"Windows Kits", WinKitVersion, "UnionMetadata", sdkVersion},
	}
	// Common entries to all platforms.
	// vcToolsInstallDir needs to end with a path separator.
	vcToolsInstallDir := vcToolsParts
	vcToolsInstallDir[len(vcToolsInstallDir)-1] += string(filepath.Separator)
	env := map[string][][]string{
		"VSINSTALLDIR":      {{`.\`}},
		"VCINSTALLDIR":      {{`VC\`}},
		"INCLUDE":           includeDirs,
		"LIBPATH":           libPathDirs,
		"VCToolsInstallDir": {vcToolsInstallDir},
	}
	envX86 := map[string][][]string{
		"PATH": {
			{"Windows Kits", WinKitVersion, "bin", sdkVersion, "x64"},
			append(vcToolsParts, "bin", "HostX64", "x86"),
			append(vcToolsParts, "bin", "HostX64", "x64"), // Needed for mspdb1x0.dll.
		},
		"LIB": {
			append(vcToolsParts, "lib", "x86"),
			append(vcToolsParts, "atlmfc", "lib", "x86"),
			{"Windows Kits", WinKitVersion, "Lib", sdkVersion, "um", "x86"},
			{"Windows Kits", WinKitVersion, "Lib", sdkVersion, "ucrt", "x86"},
		},
	}
	envX64 := map[string][][]string{
		"PATH": {
			{"Windows Kits", WinKitVersion, "bin", sdkVersion, "x64"},
			append(vcToolsParts, "bin", "HostX64", "x64"),
		},
		"LIB": {
			append(vcToolsParts, "lib", "x64"),
			append(vcToolsParts, "atlmfc", "lib", "x64"),
			{"Windows Kits", WinKitVersion, "Lib", sdkVersion, "um", "x64"},
			{"Windows Kits", WinKitVersion, "Lib", sdkVersion, "ucrt", "x64"},
		},
	}
	envARM64 := map[string][][]string{
		"PATH": {
			{"Windows Kits", WinKitVersion, "bin", sdkVersion, "x64"},
			append(vcToolsParts, "bin", "HostX64", "arm64"),
			append(vcToolsParts, "bin", "HostX64", "x64"),
		},
		"LIB": {
			append(vcToolsParts, "lib", "arm64"),
			{"Windows Kits", WinKitVersion, "Lib", sdkVersion, "um", "arm64"},
			{"Windows Kits", WinKitVersion, "Lib", sdkVersion, "ucrt", "arm64"},
		},
	}
	genPath := func(dirs [][]string) string {
		retStr := ""
		for _, dirEntry := range dirs {
			retStr += `%cd%` + string(filepath.Separator)
			retStr += filepath.Join(dirEntry...)
			retStr += ";"
		}
		if strings.HasSuffix(retStr, ";") {
			return retStr[:len(retStr)-1]
		}
		return retStr
	}
	setEnvPrefix := filepath.Join(tmpDir, "SetEnv")

	// Write cmd file.
	setCmd, err := os.Create(setEnvPrefix + ".cmd")
	if err != nil {
		return "", err
	}
	defer setCmd.Close()
	genEnv := func(dirMap map[string][][]string) string {
		var buffer bytes.Buffer
		keys := make([]string, 0)
		for envvar := range dirMap {
			keys = append(keys, envvar)
		}
		sort.Strings(keys)
		for _, envvar := range keys {
			dirs := dirMap[envvar]
			if envvar == "PATH" {
				fmt.Fprintf(&buffer, "set %s=%s;%%PATH%%;\n", envvar, genPath(dirs))
			} else {
				fmt.Fprintf(&buffer, "set %s=%s\n", envvar, genPath(dirs))
			}
		}
		return buffer.String()
	}
	templateVars := struct {
		SharedEnv string
		X86Env    string
		X64Env    string
		ARM64Env  string
	}{
		SharedEnv: genEnv(env),
		X86Env:    genEnv(envX86),
		X64Env:    genEnv(envX64),
		ARM64Env:  genEnv(envARM64),
	}
	templateText := `@echo off
:: Generated by winsdk, do not modify
pushd %~dp0..\..\..
{{.SharedEnv}}

if "%1"=="/x64" goto x64
if "%1"=="/arm64" goto arm64

{{.X86Env}}
goto :END

:x64
{{.X64Env}}
goto :END

:arm64
{{.ARM64Env}}
goto :END

:END
popd
`
	tmpl, err := template.New("batch").Parse(templateText)
	if err != nil {
		return "", err
	}
	if err := tmpl.Execute(setCmd, templateVars); err != nil {
		return "", err
	}

	// Write JSON files which will be used by Fuchsia windows_sdk recipe module
	// located at https://fuchsia.googlesource.com/infra/recipes/+/refs/heads/main/recipe_modules/windows_sdk/api.py
	mapMerge := func(envvarA, envvarB map[string][][]string) (map[string][][]string, error) {
		retMap := make(map[string][][]string)
		for entry, val := range envvarA {
			retMap[entry] = val
		}
		for entry, val := range envvarB {
			if _, ok := retMap[entry]; ok {
				return nil, fmt.Errorf("env maps should not have intersection")
			}
			retMap[entry] = val
		}
		return retMap, nil
	}

	// TODO(fxbug.dev/99600): Consider to remove this extra layer once builders
	// migrate to this new SDK package.
	jsonOutputWrapper := func(envvar map[string][][]string) map[string]map[string][][]string {
		retMap := make(map[string]map[string][][]string)
		retMap["env"] = envvar
		return retMap
	}

	setX86JSON, err := os.Create(setEnvPrefix + ".x86.json")
	if err != nil {
		return "", err
	}
	defer setX86JSON.Close()
	x86envJSON, err := mapMerge(env, envX86)
	if err != nil {
		return "", err
	}
	if err := json.NewEncoder(setX86JSON).Encode(jsonOutputWrapper(x86envJSON)); err != nil {
		return "", err
	}

	setX64JSON, err := os.Create(setEnvPrefix + ".x64.json")
	if err != nil {
		return "", err
	}
	defer setX64JSON.Close()
	x64envJSON, err := mapMerge(env, envX64)
	if err != nil {
		return "", err
	}
	if err := json.NewEncoder(setX64JSON).Encode(jsonOutputWrapper(x64envJSON)); err != nil {
		return "", err
	}

	setARM64JSON, err := os.Create(setEnvPrefix + ".arm64.json")
	if err != nil {
		return "", err
	}
	defer setARM64JSON.Close()
	arm64envJSON, err := mapMerge(env, envX64)
	if err != nil {
		return "", err
	}
	if err := json.NewEncoder(setARM64JSON).Encode(jsonOutputWrapper(arm64envJSON)); err != nil {
		return "", err
	}

	*files = append(*files, packedFile{filepath.Join(tmpDir, "SetEnv.cmd"), filepath.Join("Windows Kits", WinKitVersion, "bin", "SetEnv.cmd")})
	*files = append(*files, packedFile{filepath.Join(tmpDir, "SetEnv.x86.json"), filepath.Join("Windows Kits", WinKitVersion, "bin", "SetEnv.x86.json")})
	*files = append(*files, packedFile{filepath.Join(tmpDir, "SetEnv.x64.json"), filepath.Join("Windows Kits", WinKitVersion, "bin", "SetEnv.x64.json")})
	*files = append(*files, packedFile{filepath.Join(tmpDir, "SetEnv.arm64.json"), filepath.Join("Windows Kits", WinKitVersion, "bin", "SetEnv.arm64.json")})
	vsVersionFile := filepath.Join(tmpDir, "VS_VERSION")
	fd, err := os.Create(vsVersionFile)
	if err != nil {
		return tmpDir, err
	}
	defer fd.Close()
	fmt.Fprint(fd, vsVersion)
	*files = append(*files, packedFile{filepath.Join(tmpDir, "VS_VERSION"), filepath.Join("Windows Kits", WinKitVersion, "bin", "VS_VERSION")})
	return tmpDir, nil
}

func walkSDKFiles(files []packedFile, filter string, fn func(entry packedFile) error) ([]byte, error) {
	var totalSize, count int64
	lockFileCreator := newLockFileCreator()
	missingFile := false
	for _, entry := range files {
		simplified := entry.origin
		if len(simplified) > 40 {
			simplified = simplified[len(simplified)-40:]
		}
		fmt.Printf("\r %d/%d ...%s", count, len(files), simplified)

		info, err := os.Stat(entry.origin)
		if err != nil {
			if os.IsNotExist(err) {
				missingFile = true
				fmt.Fprintf(os.Stderr, "\r%s does not exist.\n\n", entry.origin)
			} else {
				return nil, err
			}
		}
		totalSize += info.Size()
		count++

		if err := fn(entry); err != nil {
			return nil, err
		}
		if filter == "" || !strings.HasPrefix(entry.origin, filter) {
			// Skip envvar batch file and JSON files from lockfile
			// hashes calculation
			inputFile, err := os.Open(entry.origin)
			if err != nil {
				return nil, err
			}
			defer inputFile.Close()
			lockFileCreator.addFile(entry, inputFile)
		}
	}
	fmt.Println()
	fmt.Printf("\n %1.3f GB of data in %d files", float64(totalSize)/1e9, len(files))
	if missingFile {
		return nil, fmt.Errorf("missing files in SDK package")
	}
	return lockFileCreator.generateLockFile()
}

func generageSDKDir(files []packedFile, envDir string) error {
	// Write SDK package directory in place instead of moving
	// it from temporary directory as os.Rename behaves differently
	// for directories.
	if _, err := os.Stat(outputPath); !os.IsNotExist(err) {
		os.RemoveAll(outputPath)
	}
	if err := os.MkdirAll(outputPath, 0755); err != nil {
		return err
	}
	lockFileData, err := walkSDKFiles(files, envDir, func(entry packedFile) error {
		if err := os.MkdirAll(filepath.Dir(filepath.Join(outputPath, entry.target)), 0755); err != nil {
			return err
		}
		targetFile, err := os.Create(filepath.Join(outputPath, entry.target))
		if err != nil {
			return err
		}
		inputFile, err := os.Open(entry.origin)
		if err != nil {
			return err
		}
		defer inputFile.Close()
		if _, err := io.Copy(targetFile, inputFile); err != nil {
			return err
		}
		return nil
	})
	if err != nil {
		return err
	}
	lockFile, err := os.Create(filepath.Join(outputPath, "content.lock"))
	if err != nil {
		return err
	}
	defer lockFile.Close()
	lockFile.Write(lockFileData)
	return nil
}

func generateSDKArchive(files []packedFile, envDir string) (string, error) {
	outputFile, err := os.CreateTemp("", "sdkpack.zip")
	if err != nil {
		return outputFile.Name(), err
	}
	defer outputFile.Close()
	zipWriter := zip.NewWriter(outputFile)
	defer zipWriter.Close()
	lockFileData, err := walkSDKFiles(files, envDir, func(entry packedFile) error {
		compressedFile, err := zipWriter.Create(entry.target)
		if err != nil {
			return err
		}
		inputFile, err := os.Open(entry.origin)
		if err != nil {
			return err
		}
		defer inputFile.Close()
		if _, err = io.Copy(compressedFile, inputFile); err != nil {
			return err
		}
		return nil
	})
	if err != nil {
		return outputFile.Name(), err
	}

	lockFile, err := zipWriter.Create("content.lock")
	if err != nil {
		return outputFile.Name(), err
	}
	lockFile.Write(lockFileData)
	return outputFile.Name(), nil
}

func generateSDK() error {
	vsPath, err := getVSPath()
	if err != nil {
		return err
	}
	tmpVSToolsPath, err := expandWildcards(vsPath, filepath.Join("VC", "Tools", "MSVC", VcrtVersionWildcard))
	if err != nil {
		return err
	}
	vcToolsPath := tmpVSToolsPath[len(vsPath)+1:]

	fmt.Printf("Building file list for VS %s and Windows SDK %s\n", vsVersion, sdkVersion)
	files, err := buildFileList(vsPath, vcToolsPath, VcrtVersionWildcard)
	if err != nil {
		return err
	}

	if dryRun {
		lockFileData, err := walkSDKFiles(files, "", func(entry packedFile) error {
			return nil
		})
		if err != nil {
			return err
		}
		fmt.Printf("%s\n", string(lockFileData))
		return nil
	}

	tmpDir, err := addEnvSetup(&files, vcToolsPath)
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmpDir)
	if archivePath != "" {
		tempOutput, err := generateSDKArchive(files, tmpDir)
		if err != nil {
			return err
		}
		return os.Rename(tempOutput, archivePath)
	}
	if outputPath != "" {
		return generageSDKDir(files, tmpDir)
	}
	return nil
}

func main() {
	if runtime.GOOS != "windows" {
		fmt.Println("this program only works under Windows")
		os.Exit(0)
	}
	flag.Usage = func() {
		fmt.Fprintf(flag.CommandLine.Output(), "usage: %s [options]\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()
	if help {
		flag.Usage()
		os.Exit(0)
	}

	if outputPath == "" && archivePath == "" {
		outputPath = "winsdk"
	} else if outputPath != "" && archivePath != "" {
		fmt.Fprintf(os.Stderr, "output and archive flags cannot be used together\n")
		flag.Usage()
		os.Exit(1)
	}

	if err := generateSDK(); err != nil {
		fmt.Fprintf(os.Stderr, "%v", err)
		os.Exit(1)
	}
}
