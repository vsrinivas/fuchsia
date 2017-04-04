# Mozart Screenshot Tool

This directory contains screencap, a simple tool for capturing a screenshot
of the composition being rendered.

## USAGE

    screencap <file path>

### Arguments

    --renderer=[<renderer_index>] Mozart compositor can render multiple scenes
                                  at once, specify the index of the one to
                                  capture a screenshot of. Index starts at 0.
                                  Default value is 0.

### Examples

    screencap --renderer=2 /tmp/screencap.png
