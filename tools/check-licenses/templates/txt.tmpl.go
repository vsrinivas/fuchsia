// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const TemplateTxt = `
{{ range $_, $license := .Used }}
================================================================================
License Category: {{ (getCategory $license) }}
{{ range $author := (getAuthors $license ) }}
--------------------------------------------------------------------------------
Authors/Contributors: {{ $author }}

{{ (getText $license $author) }}
{{ end }}
================================================================================
{{ end }}
`
