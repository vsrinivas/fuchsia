#!/usr/bin/env bash

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

seconds=5 #"Length of the video in seconds"
frame_rate=60
filename="test_video.webm"
font_file="/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf"
title="web_engine test video"
video_size="640x480"


ffmpeg -f lavfi \
    -i testsrc=duration="${seconds}":size="${video_size}":rate="${frame_rate}" \
    -c:v libvpx \
    -pix_fmt yuv420p \
    -vf "fps=${frame_rate},
  drawtext=fontfile='${font_file}':fontcolor=black:fontsize=60:x=100:y=(h-text_h)/2:text='%{eif\:(t)\:d}.%{eif\:(mod(t, 1)*pow(10,2))\:d\:2}/%{eif\:(${seconds})\:d}.00',pad=ceil(iw/2)*2:ceil(ih/2)*2,
  drawtext=fontfile='${font_file}':fontcolor=black:fontsize=25:x=(w-text_w)/2:y=50:text='${title}',pad=ceil(iw/2)*2:ceil(ih/2)*2
  " -y $filename;