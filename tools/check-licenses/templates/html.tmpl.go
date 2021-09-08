// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const TemplateHtml = `
<html>
<style>
	body {
		background-color: #ffffff
	}
</style>
<meta charset="utf-8"/>
<body>

<div style="width: 1000">
Table of Contents:
<ol>{{ range $_, $license := .Used }}<li><a href="#{{ (getCategory $license) }}">{{ (getCategory $license) }}</a></li>{{ end }}</ol>
{{ range $_, $license := .Used }}<br />
{{ range $match := (getMatches $license) }}
<div id="{{ (getCategory $license) }}" style="background-color: #eeeeee">
<br>
Notices for file(s), reference: {{ (getCategory $license) }}:<ul>
<li> License Category: {{ (getCategory $license) }} </li>
<li>Authors/Contributors: {{ (getCopyrights $match) }}</li>
</ul>
{{ (getHTMLText $match) }}<br />
</div>
{{ end }}
{{ end }}
</div>

</body>
</html>
`
