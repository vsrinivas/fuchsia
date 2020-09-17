// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const TemplateHtml = `
<html>

<div style="width: 1000">
Table of Contents:
<ol>{{ range $file, $licenses := .TableOfContents }}<li>{{ $file }} (
	{{ range $_, $license := $licenses }} <a href="#{{ (getCategory $license) }}">{{ (getCategory $license) }}</a>, {{ end }})</li>{{ end }}</ol>
{{ range $_, $license := .Used }}<br />
<div id="{{ (getCategory $license) }}" style="background-color: #eeeeee">
Notices for file(s), reference: {{ (getCategory $license) }}:<ul>
{{ range $author := (getAuthors $license) }}<li>Authors/Contributors: {{ $author }}</li>
<li> License Category: {{ (getCategory $license) }} </li>
{{ range $files := (getFiles $license $author) }}<li>{{ $files }}</li>
{{ end }}
</ul>
{{ (getHTMLText $license $author) }}<br />
</div>
{{ end }}
{{ end }}
</div>

</body>
</html>
`
