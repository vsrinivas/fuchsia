// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let html = `<!doctype html>
<html>
<head>
  <title>Video MP4</title>
</head>
<body style="height:100%">
  <!-- Video is Big Buck Bunny. Attribution: (c) copyright 2008, Blender Foundation / www.bigbuckbunny.org -->
  <!-- Note that "autoplay" only works if video is "muted". -->
	<video src="http://127.0.0.1:81/webm" type="video/webm" controls playsinline autoplay muted loop></video>
</body>
</html>`;

document.write(html);
