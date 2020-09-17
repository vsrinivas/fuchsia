// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const TemplateJson = `{
  "licenses": {
    "unused": {
			"categories": [{{ range $_, $license := .Unused }}
				"{{ (getCategory $license) }}",{{ end }}
			],
		},
    "used": {
			"license": [
				{{ range $_, $license := .Used }}{
				{{ range $author := (getAuthors $license) }}
				"authors": "{{ $author }}",
				"category": "{{ (getCategory $license) }}",
				"files": [{{ range $file := (getFiles $license $author) }}
					"{{ $file }}",{{ end }}
				],
				"text": "{{ (getEscapedText $license $author) }}",
				},{{ end }}{{ end }}
      ]
    }
	},
	"files": {
		"unlicensed": []
	}
}
`
