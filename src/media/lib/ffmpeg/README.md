[FFMpeg prebuilts]

This target uses prebuilt artifacts for the ffmpeg library which are downloaded
via CIPD.  To update these, identify a version of these artifacts from the
builder bot at
[https://ci.chromium.org/p/fuchsia/builders/luci.fuchsia.ci/ffmpeg](https://ci.chromium.org/p/fuchsia/builders/luci.fuchsia.ci/ffmpeg)
and update the `git_revision:...` tag in //integration/prebuilts.
