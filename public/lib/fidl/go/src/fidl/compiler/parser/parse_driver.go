// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fmt"
	"io/ioutil"
	"fidl/compiler/core"
	"os"
	"path/filepath"
)

///////////////////////////////////////////////////////////////////////
/// Type ParseDriver
//////////////////////////////////////////////////////////////////////

// A ParseDriver is used to parse a list of .mojom files.
//
// Construct a new ParseDriver via NewDriver() and then call ParseFiles()
// passing in the list of top-level .mojom files to be parsed. Any imported
// files will also be parsed.
//
// We attempt to find the file named by a given path, both top-level and
// imported, using the following algorithm:
// (1) If the specified path is an absolute path we use that path
// (2) Otherwise if the file was imported from another file we first attempt
// to find a file with the specified path relative to the directory of the
// importing file
// (3) Otherwise if the file was imported we attempt to find a file with the
// specified path relative to one of the specified import directories.
// (4) Otherwise we attempt to find a file with the specified path relative to
// the current working directory.
//
// After all files have been parsed the populated |MojomDescriptor| will be
// resovled, and then Mojo serialization data will be computed for use by
// the code generators.
//
// A poulated |MojomDescriptor| is returned.
// If there  was an error then the returned |err| will be non-nil.
//
// A ParseDriver may only be used once.
type ParseDriver struct {
	fileProvider     FileProvider
	fileExtractor    FileExtractor
	parseInvoker     ParseInvoker
	importDirs       []string
	debugMode        bool
	metaDataOnlyMode bool
}

// NewDriver consructs a new ParseDriver.
//
// importDirectories is a list of paths, either absolute or relative to the
// current working directory, of directories in which we should search for
// imported .mojom files.
//
// If debugMode is true we print to standard out the parse tree resulting
// from each file parsing. In non-debug mode the parsers do not explicitly
// construct a parse tree.
//
// If metaDataOnlyMode is true then:
// (1) The parsers will only parse the mojom file metadata (module statement,
// import statements, file-level attributes). The mojom declarations will not
// be parsed. The result will be as if the .mojom file did not have any mojom
// declarations. The resolution and validation phases will not be executed.
// (2) References to imported files will not be followed and imported files
// will not be parsed. Only explicitly specified files will be parsed.
// This implies that the |MojomFile|s will contain partial information about
// their imports. Each |ImportedFile| will contain its |SpecifiedName| but not
// its |CanonicalFileName|.
func NewDriver(importDirectories []string, debugMode, metaDataOnlyMode bool) *ParseDriver {
	fileProvider := new(OSFileProvider)
	fileProvider.importDirs = importDirectories
	return newDriver(importDirectories, debugMode, metaDataOnlyMode, fileProvider,
		DefaultFileExtractor(0), DefaultParseInvoker(0))
}

// This version of the factory is used in tests.
func newDriver(importDirectories []string, debugMode, metaDataOnlyMode bool, fileProvider FileProvider,
	fileExtractor FileExtractor, parseInvoker ParseInvoker) *ParseDriver {
	p := ParseDriver{fileProvider: fileProvider, fileExtractor: fileExtractor, parseInvoker: parseInvoker,
		importDirs: importDirectories, debugMode: debugMode, metaDataOnlyMode: metaDataOnlyMode}
	return &p

}

// Parses each of the given .mojom files and all of the files in the
// import graph rooted by each file. A single MojomDescriptor is created and
// populated with the result of parsing all of those files. If the parsing is
// successful then err will be nil.
//
// fileNames must not be nil or we will panic.
func (d *ParseDriver) ParseFiles(fileNames []string) (descriptor *core.MojomDescriptor, err error) {
	if fileNames == nil {
		// We panic instead of returning an error here because this would be a programming error
		// as opposed to an error in the input.
		panic("fileNames may not be nil.")
	}
	filesToProcess := make([]*FileReference, len(fileNames))
	descriptor = core.NewMojomDescriptor()
	for i, fileName := range fileNames {
		filesToProcess[i] = &FileReference{specifiedPath: fileName}
	}

	for len(filesToProcess) > 0 {
		currentFile := filesToProcess[0]
		filesToProcess = filesToProcess[1:]
		if err = d.fileProvider.findFile(currentFile); err != nil {
			return
		}

		var importedFrom *core.MojomFile = nil
		if currentFile.importedFrom != nil {
			importedFrom = currentFile.importedFrom.mojomFile
			// Tell the importing file about the absolute path of the imported file.
			// Note that we must do this even if the imported file has already been processed
			// because a given file may be imported by multiple files and each of those need
			// to be told about the absolute path of the imported file.
			importedFrom.SetCanonicalImportName(currentFile.specifiedPath, currentFile.absolutePath)
		}

		if !descriptor.ContainsFile(currentFile.absolutePath) {
			contents, fileReadError := d.fileProvider.provideContents(currentFile)
			if fileReadError != nil {
				err = fileReadError
				return
			}
			// topLevelFileName should be non-empty if and only if the current file is a top-level file.
			topLevelFileName := ""
			if importedFrom == nil {
				topLevelFileName = currentFile.specifiedPath
			}
			parser := MakeParser(currentFile.absolutePath, topLevelFileName,
				contents, descriptor, importedFrom)
			parser.SetDebugMode(d.debugMode)
			parser.SetMetaDataOnlyMode(d.metaDataOnlyMode)
			// Invoke parser.Parse() (but skip doing so in tests sometimes.)
			d.parseInvoker.invokeParse(&parser)

			if d.debugMode {
				fmt.Printf("\nParseTree for %s:", currentFile)
				fmt.Println(parser.GetParseTree())
			}

			if !parser.OK() {
				err = parser.GetError()
				return
			}
			currentFile.mojomFile = d.fileExtractor.extractMojomFile(&parser)
			if !d.metaDataOnlyMode {
				// In meta-data-only mode we do not follow import statements.
				for _, importedFile := range currentFile.mojomFile.Imports {
					// Note that it is important that we append all of the imported files here even
					// if some of them have already been processed. That is because when the imported
					// file is pulled from the queue it will be pre-processed during which time the
					// absolute path to the file will be discovered and this absolute path will be
					// set in |mojomFile| which is necessary for serializing mojomFile.
					filesToProcess = append(filesToProcess,
						&FileReference{importedFrom: currentFile, specifiedPath: importedFile.SpecifiedName})
				}
			}
		}
	}

	// Perform type and value resolution
	if err = descriptor.Resolve(); err != nil {
		return
	}

	// Compute data for generators.
	if err = descriptor.ComputeFinalData(); err != nil {
		return
	}

	// Check for ill-founded types.
	err = descriptor.DetectIllFoundedTypes()
	return
}

type FileReference struct {
	mojomFile     *core.MojomFile
	importedFrom  *FileReference
	specifiedPath string
	absolutePath  string
	directoryPath string
}

func (f FileReference) String() string {
	if f.importedFrom != nil {
		return fmt.Sprintf("%s imported from file %s.",
			f.specifiedPath, f.importedFrom.specifiedPath)
	} else {
		return fmt.Sprintf("%s", f.specifiedPath)
	}
}

// FileExtractor is an abstraction that allows us to inject fake MojomFiles
// in tests.
type FileExtractor interface {
	extractMojomFile(parser *Parser) *core.MojomFile
}

type DefaultFileExtractor int

func (DefaultFileExtractor) extractMojomFile(parser *Parser) *core.MojomFile {
	return parser.GetMojomFile()
}

// ParseInvoker is an abstraction that allows us to skip actually invoking
// the parse method in tests of the driver.
type ParseInvoker interface {
	invokeParse(parser *Parser)
}

type DefaultParseInvoker int

func (DefaultParseInvoker) invokeParse(parser *Parser) {
	parser.Parse()
}

// FileProvider is an abstraction that allows us to mock out the file system
// in tests.
type FileProvider interface {
	provideContents(fileRef *FileReference) (contents string, fileReadError error)

	// findFile attempts to locate the file specified by the |specifiedPath|
	// field of |fileRef|, taking into consideration also the |importedFrom|
	// field. If the file can be located then the |absolutePath| and
	// |directoryPath| path fields will be set and nil is returned.
	// Otherwise a non-nil error is returned.
	findFile(fileRef *FileReference) error
}

type OSFileProvider struct {
	importDirs []string
}

func (p OSFileProvider) provideContents(fileRef *FileReference) (contents string, fileReadError error) {
	data, err := ioutil.ReadFile(fileRef.absolutePath)
	if err != nil {
		fileReadError = fmt.Errorf("\nError while reading %s: %s\n\n", fileRef, err)
	} else {
		contents = string(data)
	}
	return
}

// findFile populates the |absolutePath| and |directoryPath| fields of
// *fileRef. It attempts to find a file on the file system named by the |specifiedPath|
// field using the search algorithm described at the top of this file.
func (p *OSFileProvider) findFile(fileRef *FileReference) (err error) {
	// If this FileReference has already been processed there is nothing to do.
	if len(fileRef.absolutePath) > 0 {
		return
	}

	// If the specified path is already absolute we use that path.
	if filepath.IsAbs(fileRef.specifiedPath) {
		fileRef.absolutePath = fileRef.specifiedPath
		fileRef.directoryPath = filepath.Dir(fileRef.absolutePath)
		return
	}

	// If the file was imported from another file...
	if fileRef.importedFrom != nil {
		// First attempt to find the file relative to the directory of the
		// importing file.
		attemptedName := filepath.Join(fileRef.importedFrom.directoryPath, fileRef.specifiedPath)
		if isFile(attemptedName) {
			fileRef.absolutePath, err = filepath.Abs(attemptedName)
			fileRef.directoryPath = filepath.Dir(fileRef.absolutePath)
			return
		}

		// then search in the specified import directories.
		if p.importDirs != nil {
			for _, dir := range p.importDirs {
				attemptedName := filepath.Join(dir, fileRef.specifiedPath)
				if isFile(attemptedName) {
					fileRef.absolutePath, err = filepath.Abs(attemptedName)
					fileRef.directoryPath = filepath.Dir(fileRef.absolutePath)
					return
				}
			}

		}
	}

	// Finally look in the current working directory.
	if isFile(fileRef.specifiedPath) {
		if fileRef.absolutePath, err = filepath.Abs(fileRef.specifiedPath); err != nil {
			return err
		}
		fileRef.directoryPath = filepath.Dir(fileRef.absolutePath)
		return
	}

	return fmt.Errorf("File not found: %s", fileRef)
}

func isFile(path string) bool {
	info, err := os.Stat(path)
	if err != nil {
		return false
	}
	return !info.IsDir()
}
