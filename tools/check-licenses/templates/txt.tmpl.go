// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const TemplateTxt = `
UNUSED LICENSES:
{{ range $_, $license := .Unused }}
================================================================================
Category: {{ (getCategory $license) }}
--------------------------------------------------------------------------------
License:
{{ (getPattern $license) }}
================================================================================
{{ end }}

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

USED LICENSES:
{{ range $_, $license := .Used }}
================================================================================
Category: {{ (getCategory $license) }}
{{ range $file := (getFiles $license) }}FILE: {{ $file }}
{{ end }}--------------------------------------------------------------------------------
License:
{{ (getText $license) }}
================================================================================
{{ end }}
`
