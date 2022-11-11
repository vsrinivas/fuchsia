// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let html = `<!doctype html>
<html>
<head>
  <title>Fuchsia Logo</title>
</head>
<body style="height:100%">
  <!-- The image is the fuchsia logo png. -->
  <img id="fuchsia logo" src="http://127.0.0.1:81/png" />
</body>
</html>`;

document.write(html);
