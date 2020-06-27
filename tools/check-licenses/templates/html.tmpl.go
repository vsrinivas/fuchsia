// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const TemplateHtml = `
<html>

<div style="width: 1000">
{{ range $_, $license := .Used }}<br />
<div style="background-color: #eeeeee">
Notices for file(s), reference: {{ (getCategory $license) }}:<ul>
{{ range $file := (getFiles $license) }}<li>{{ $file }}</li>
{{ end }}
</ul>
{{ (getHTMLText $license) }}<br />
</div>
{{ end }}
</div>

</body>
</html>
`
