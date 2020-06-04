package lib

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/scripts/check-licenses/templates"
)

type Metrics struct {
	num_licensed   uint
	num_unlicensed uint
}

func (license_file_tree *LicenseFileTree) getProjectLicense(path string) *LicenseFileTree {
	curr := license_file_tree
	var gold *LicenseFileTree
	pieces := strings.Split(path, "/")
	pieces = pieces[:len(pieces)-1]
	for _, piece := range pieces {
		if len(curr.files) > 0 {
			gold = curr
		}
		if _, found := curr.children[piece]; !found {
			break
		}
		curr = curr.children[piece]
	}
	return gold
}

func Walk(config *Config) error {
	root := config.BaseDir
	metrics := new(Metrics)
	licenses := NewLicenses(config.LicensePatternDir)
	my_file, my_file_err := os.Create(config.OutputFilePrefix + "." + config.OutputFileExtension)
	var license_file_tree LicenseFileTree
	license_file_tree.Init()
	if my_file_err != nil {
		fmt.Printf("file error")
	}
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			fmt.Printf("prevent panic by handling failure accessing a path %q: %v\n", path, err)
			return err
		}
		// TODO regex instead of exact match
		if info.IsDir() {
			for _, skipDir := range config.SkipDirsRegex {
				if info.Name() == skipDir {
					fmt.Printf("skipping a dir without errors: %+v \n", info.Name())
					return filepath.SkipDir
				}
			}
		} else {
			for _, skipFile := range config.SkipFilesRegex {
				if info.Name() == skipFile {
					fmt.Printf("skipping a file without errors: %+v \n", info.Name())
					return nil
				}
			}
		}

		if info.IsDir() {
			folder_licenses, folder_licenses_err := filepath.Glob(path + "/LICENSE*")
			if folder_licenses_err != nil {
				fmt.Println("Error")
			}
			if folder_licenses != nil {
				fmt.Println(" + folder_licenses:")
				fmt.Println(folder_licenses)
				license_file_tree.upsert(path, folder_licenses)
				// TODO add license files to licenses (*Licenses) object, deriving type
			}
			return nil
		}

		extension := filepath.Ext(path)
		if len(extension) == 0 {
			return nil
		}
		if _, found := config.TextExtensions[extension[1:]]; !found {
			return nil
		}

		fmt.Printf("visited file or dir: %q\n", path)
		file, open_err := os.Open(path)
		if open_err != nil {
			fmt.Printf("error opening: %v\n", open_err)
		}
		defer file.Close()
		data := make([]byte, config.MaxReadSize)
		count, open_err := file.Read(data)
		if open_err != nil {
			fmt.Printf("error reading: %v\n", open_err)
		}

		is_matched := false
		// TODO async regex pattern matching
		for i, license := range licenses.licenses {
			matched := license.pattern.Find(data[:count])
			if matched != nil {
				is_matched = true
				metrics.num_licensed++
				// TODO capture the full license
				// TODO track more than one match per pattern
				// TODO surface these matches later
				// TODO licenses.licenses[i] could probably be a pointer instead
				if len(licenses.licenses[i].matches) == 0 {
					licenses.licenses[i].matches = append(licenses.licenses[i].matches, Match{})
				}
				licenses.licenses[i].matches[0].value = matched
				licenses.licenses[i].matches[0].files = append(licenses.licenses[i].matches[0].files, path)
				fmt.Printf("  - %s\n", matched)
				break
			}
		}

		// TODO only check for project level license if file doesn't have a license
		project := license_file_tree.getProjectLicense(path)
		if !is_matched {
			if project == nil {
				metrics.num_unlicensed++
			} else {
				// TODO derive one of the project licenses
			}
		}

		// TODO if no license match, use nearest matching LICENSE file
		return nil
	})

	if err != nil {
		fmt.Printf("error walking the path %q: %v\n", root, err)
		return nil
	}

	// TODO modularize used and unused licenses
	var unused []*License
	var used []*License
	for i := range licenses.licenses {
		license := licenses.licenses[i]
		if len(license.matches) == 0 {
			unused = append(unused, &license)
		} else {
			used = append(used, &license)
		}
	}

	object := struct {
		Signature string
		Used      []*License
		Unused    []*License
	}{
		"SiGnAtUrE",
		used,
		unused,
	}
	var templateStr string
	switch config.OutputFileExtension {
	case "txt":
		templateStr = templates.TemplateTxt
	case "htm":
		templateStr = templates.TemplateHtml
	default:
		panic("no template found")
	}
	tmpl := template.Must(template.New("name").Funcs(template.FuncMap{
		"getPattern": func(license *License) string { return (*license).pattern.String() },
		"getText":    func(license *License) string { return string((*license).matches[0].value) },
		"getFiles": func(license *License) *[]string {
			var files []string
			for _, match := range license.matches {
				for _, file := range match.files {
					files = append(files, file)
				}
			}
			return &files
		},
	}).Parse(templateStr))
	tmpl_err := tmpl.Execute(my_file, object)
	if tmpl_err != nil {
		panic(err)
	}

	fmt.Printf(
		"Metrics:\nNumber licensed: %d\nNumber unlicensed: %d\n",
		metrics.num_licensed,
		metrics.num_unlicensed)
	return nil
}

// TODO tools/zedmon/client/pubspec.yaml" error reading: EOF
