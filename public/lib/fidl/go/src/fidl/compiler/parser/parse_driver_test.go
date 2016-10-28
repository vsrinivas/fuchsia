// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fmt"
	"fidl/compiler/core"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

// FakeFileProvider implements FileProvider.
type FakeFileProvider struct {
	// This field records the names of the files whose contents were requested.
	requestedFileNames []string
}

// FakeFileProvider implements provideContents by recording the name of the file
// whose contents are being requested and then returning the empty string.
func (f *FakeFileProvider) provideContents(fileRef *FileReference) (contents string, fileReadError error) {
	f.requestedFileNames = append(f.requestedFileNames, fileRef.specifiedPath)
	return "", nil
}

// FakeFileProvider implements findFile by always succeeding to find the
// file and reporting that the |absolutePath| is equal to the |specifiedPath|
// with the prefix "/a/b/c/"
func (f *FakeFileProvider) findFile(fileRef *FileReference) error {
	fileRef.absolutePath = fmt.Sprintf("/a/b/c/%s", fileRef.specifiedPath)
	return nil
}

type NoOpParseInvoker int

func (NoOpParseInvoker) invokeParse(parser *Parser) {}

// FakeFileExtractor implements FileExtractor
type FakeFileExtractor struct {
	// This field maps a file name to the set of import names that we want to
	// simulate the given file is importing.
	importNames map[string][]string
}

func makeFakeFileExtractor() FakeFileExtractor {
	extractor := FakeFileExtractor{}
	extractor.importNames = make(map[string][]string)
	return extractor
}

// Invoke this method to specify that FakeFileExtractor should simulate the fact
// that the file named |fileName| imports the files specified by |imports|.
func (f *FakeFileExtractor) appendImportsToFile(fileName string, imports ...string) {
	if imports != nil {
		f.importNames[fileName] = append(f.importNames[fileName], imports...)
	}
}

// FakeFileExtractor implements extractMojomFile() by returning an instance
// of MojomFile that has had imports added to it according to the values
// in the map |importNames|.
func (f *FakeFileExtractor) extractMojomFile(parser *Parser) *core.MojomFile {
	file := parser.GetMojomFile()
	for _, importName := range f.importNames[file.CanonicalFileName] {
		file.AddImport(core.NewImportedFile(importName, nil))
	}
	return file
}

// TestExpectedFilesParsed tests the logic in function ParseDriver.ParseFiles()
// for determining which files should be parsed.
func TestExpectedFilesParsed(t *testing.T) {
	// Construct our fake objects.
	fakeFileProvider := FakeFileProvider{}
	fakeFileExtractor := makeFakeFileExtractor()
	// Our fake file1 will import file3, file4, file5
	fakeFileExtractor.appendImportsToFile("/a/b/c/file1", "file3", "file4", "file5")
	// Our fake file5 will import file1 and file6
	fakeFileExtractor.appendImportsToFile("/a/b/c/file5", "file1", "file6")

	// Construct the driver under test
	driver := newDriver([]string{}, false, false, &fakeFileProvider, &fakeFileExtractor, NoOpParseInvoker(0))

	// Invoke ParseFiles with file1, file2 and file3 as top-level files.
	descriptor, err := driver.ParseFiles([]string{"file1", "file2", "file3"})
	if err != nil {
		t.Errorf(err.Error())
	}

	// Check that the correct files had their contents requested in the expected order.
	expectedFileRefs := []string{"file1", "file2", "file3", "file4", "file5", "file6"}
	if !reflect.DeepEqual(expectedFileRefs, fakeFileProvider.requestedFileNames) {
		t.Errorf("%v != %v", expectedFileRefs, fakeFileProvider.requestedFileNames)
	}

	// Retrieve the MojomFile for file1.
	file1, ok := descriptor.MojomFilesByName["/a/b/c/file1"]
	if !ok {
		t.Fatalf("file1 not found.")
	}

	// Check that it has 3 imports: file3, file4, file5
	if file1.Imports == nil {
		t.Fatalf("file1 has no imports.")
	}
	if len(file1.Imports) != 3 {
		t.Fatalf("len(file1.Imports)=%d", len(file1.Imports))
	}
	if file1.Imports[0].CanonicalFileName != "/a/b/c/file3" {
		t.Errorf("file1.Imports[0].CanonicalFileName=%q", file1.Imports[0].CanonicalFileName)
	}
	if file1.Imports[1].CanonicalFileName != "/a/b/c/file4" {
		t.Errorf("file1.Imports[1].CanonicalFileName=%q", file1.Imports[1].CanonicalFileName)
	}
	if file1.Imports[2].CanonicalFileName != "/a/b/c/file5" {
		t.Errorf("file1.Imports[1].CanonicalFileName=%q", file1.Imports[2].CanonicalFileName)
	}

	// Retrieve the MojomFile for file3
	file3, ok := descriptor.MojomFilesByName["/a/b/c/file3"]
	if !ok {
		t.Fatalf("file3 not found.")
	}

	// We expect the |ImportedFrom| field to be nil and the |SpecifiedFileName|
	// to be non-empty because, even though file3
	// was imported from file1, it was also a top-level file.
	if file3.ImportedFrom != nil {
		t.Errorf("file3.ImportedFrom == %v", file3.ImportedFrom)
	}
	if file3.SpecifiedFileName != "file3" {
		t.Errorf("file3.SpecifiedFileName == %v", file3.SpecifiedFileName)
	}

	// Retrieve the MojomFile for file4
	file4, ok := descriptor.MojomFilesByName["/a/b/c/file4"]
	if !ok {
		t.Fatalf("file4 not found.")
	}

	// We expect the |ImportedFrom| field to refer to file1 because file4 was imported
	// from file1 and was not a top-level file. We expect |SpecifiedFileName| to
	// be empty.
	if file4.ImportedFrom != file1 {
		t.Errorf("file4.ImportedFrom = %v", file4.ImportedFrom)
	}
	if file4.SpecifiedFileName != "" {
		t.Errorf("file4.SpecifiedFileName = %v", file4.SpecifiedFileName)
	}

}

// TestMetaDataOnlyMode tests the logic in function ParseDriver.ParseFiles()
// when metaDataOnlyMode is true. Imported files should not be parsed
// and CanonicalFileNames should not be set.
func TestMetaDataOnlyMode(t *testing.T) {
	// Construct our fake objects.
	fakeFileProvider := FakeFileProvider{}
	fakeFileExtractor := makeFakeFileExtractor()
	// Our fake file1 will import file3, file4, file5
	fakeFileExtractor.appendImportsToFile("/a/b/c/file1", "file3", "file4", "file5")
	// Our fake file5 will import file1 and file6
	fakeFileExtractor.appendImportsToFile("/a/b/c/file5", "file1", "file6")

	// Construct the driver under test
	driver := newDriver([]string{}, false, true, &fakeFileProvider, &fakeFileExtractor, NoOpParseInvoker(0))

	// Invoke ParseFiles with file1, file2 and file3 as top-level files.
	descriptor, err := driver.ParseFiles([]string{"file1", "file2", "file3"})
	if err != nil {
		t.Errorf(err.Error())
	}

	// Check that the correct files had their contents requested in the expected order.
	expectedFileRefs := []string{"file1", "file2", "file3"}
	if !reflect.DeepEqual(expectedFileRefs, fakeFileProvider.requestedFileNames) {
		t.Errorf("%v != %v", expectedFileRefs, fakeFileProvider.requestedFileNames)
	}

	// Retrieve the MojomFile for file1.
	file1, ok := descriptor.MojomFilesByName["/a/b/c/file1"]
	if !ok {
		t.Fatalf("file1 not found.")
	}

	// Check that it has 3 imports: file3, file4, file5
	if file1.Imports == nil {
		t.Fatalf("file1 has no imports.")
	}
	if len(file1.Imports) != 3 {
		t.Fatalf("len(file1.Imports)=%d", len(file1.Imports))
	}
	if file1.Imports[0].CanonicalFileName != "" {
		t.Errorf("file1.Imports[0].CanonicalFileName=%q", file1.Imports[0].CanonicalFileName)
	}
	if file1.Imports[1].CanonicalFileName != "" {
		t.Errorf("file1.Imports[1].CanonicalFileName=%q", file1.Imports[1].CanonicalFileName)
	}
	if file1.Imports[2].CanonicalFileName != "" {
		t.Errorf("file1.Imports[1].CanonicalFileName=%q", file1.Imports[2].CanonicalFileName)
	}

	// Retrieve the MojomFile for file3
	file3, ok := descriptor.MojomFilesByName["/a/b/c/file3"]
	if !ok {
		t.Fatalf("file3 not found.")
	}

	// We expect the |ImportedFrom| field to be nil and the |SpecifiedFileName|
	// to be non-empty because, even though file3
	// was imported from file1, it was also a top-level file.
	if file3.ImportedFrom != nil {
		t.Errorf("file3.ImportedFrom == %v", file3.ImportedFrom)
	}
	if file3.SpecifiedFileName != "file3" {
		t.Errorf("file3.SpecifiedFileName == %v", file3.SpecifiedFileName)
	}

	// Try to retrieve the MojomFile for file4. It should not be there.
	if _, ok = descriptor.MojomFilesByName["/a/b/c/file4"]; ok {
		t.Fatalf("file4 was unexpectedly found.")
	}
}

// TestOSFileProvider tests the function OSFileProvider.findFiles() and OSFileProvider.provideContents()
func TestOSFileProvider(t *testing.T) {
	// Top level files (not imported from anything) are found relative to the current directory.
	fileReferenceA := doOSFileProviderTest(t, "../test_data/a/testfile1", nil, nil, "mojom_tool/test_data/a")
	fileReferenceB := doOSFileProviderTest(t, "../test_data/b/testfile1", nil, nil, "mojom_tool/test_data/b")

	// A file imported from a file in directory 'a' should be found in directory 'a'
	doOSFileProviderTest(t, "testfile1", &fileReferenceA, nil, "mojom_tool/test_data/a")

	// A file imported from a file in directory 'b' should be found in directory 'a'
	doOSFileProviderTest(t, "testfile1", &fileReferenceB, nil, "mojom_tool/test_data/b")

	// A file imported from a file in directory 'a' should be found in directory 'b' if there is no file with the
	// specified name in directory 'a' and directory 'b' is in the search path.
	doOSFileProviderTest(t, "testfile2", &fileReferenceA, []string{"../test_data/b"}, "mojom_tool/test_data/b")

	// The file is imported from directory 'a' and directory 'b' is on the search path but there is no file with the
	// specified name in either directory and there is a file with the specified name in the current directory.
	// The file should be found in the current directory. Note that the last argument is a random string of digits that
	// we expect to find in the contents of the file parser_driver_test.go (i.e this file).
	doOSFileProviderTest(t, "parse_driver_test.go", &fileReferenceA, []string{"../test_data/b"}, "840274941330987490326243")
}

// doOSFileProviderTest is the workhorse for TestOSFileProvider.
func doOSFileProviderTest(t *testing.T, specifiedPath string, importedFrom *FileReference,
	globalImports []string, expectedContents string) FileReference {
	fileProvider := new(OSFileProvider)
	fileProvider.importDirs = globalImports
	fileReference := FileReference{specifiedPath: specifiedPath}
	fileReference.importedFrom = importedFrom

	// Invoke findFile()
	if err := fileProvider.findFile(&fileReference); err != nil {
		t.Fatalf(err.Error())
	}

	// Check that the absolutePath has been populated to the absolute path of a file.
	if fileReference.absolutePath == "" {
		t.Fatalf("absolutePath is not set for %s", fileReference.specifiedPath)
	}
	if !filepath.IsAbs(fileReference.absolutePath) {
		t.Fatalf("absolutePath is not absolute for %s: %s", fileReference.specifiedPath, fileReference.absolutePath)
	}
	info, err := os.Stat(fileReference.absolutePath)
	if err != nil {
		t.Fatalf("cannot stat absolutePath %s: %s", fileReference.absolutePath, err.Error())
	}
	if info.IsDir() {
		t.Fatalf("absolutePath refers to a directory: %s", fileReference.absolutePath)
	}

	// Check that the dirPath has been populated to the absolute path of a directory.
	if fileReference.directoryPath == "" {
		t.Fatalf("directoryPath is not set for %s", fileReference.specifiedPath)
	}
	if !filepath.IsAbs(fileReference.directoryPath) {
		t.Fatalf("directoryPath is not absolute for %s: %s", fileReference.specifiedPath, fileReference.directoryPath)
	}
	info, err = os.Stat(fileReference.directoryPath)
	if err != nil {
		t.Fatalf("cannot stat directoryPath %s: %s", fileReference.directoryPath, err.Error())
	}
	if !info.IsDir() {
		t.Fatalf("directoryPath does not refer to a directory: %s", fileReference.directoryPath)
	}
	if filepath.Dir(fileReference.absolutePath) != fileReference.directoryPath {
		t.Fatalf("wrong directoryPath expected parent of %s got %s", fileReference.absolutePath, fileReference.directoryPath)
	}

	contents, err := fileProvider.provideContents(&fileReference)
	if err != nil {
		t.Errorf("Error from provideContents for %v: %s", fileReference, err.Error())
	}
	if !strings.Contains(contents, expectedContents) {
		t.Errorf("Wrong file contents for %v. Expecting %s got %s.", fileReference, expectedContents, contents)
	}

	return fileReference
}
