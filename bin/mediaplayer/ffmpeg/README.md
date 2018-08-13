[FFMpeg prebuilts]

The media player uses prebuilt artifacts for the ffmpeg library which are downloaded by the
`update_ffmpeg_prebuilts.sh` script into prebuilts/{x64,arm64}.  To update these, first identify a
version of these artifacts from the buildbot at
[https://ci.chromium.org/p/fuchsia/builders/luci.fuchsia.ci/ffmpeg-linux](https://ci.chromium.org/p/fuchsia/builders/luci.fuchsia.ci/ffmpeg-linux)
and update the contents of the file prebuilt/version to match the `got_revision` property of the
build you'd like to use. Run the `update_ffmpeg_prebuilts.sh` script locally and rebuild to test.
