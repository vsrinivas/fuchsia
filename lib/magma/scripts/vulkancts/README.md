1. Patch mesa + magma to use jemalloc

git clone https://fuchsia.googlesource.com/third_party/jemalloc third_party/jemalloc
git fetch https://fuchsia.googlesource.com/third_party/jemalloc refs/changes/46/16946/2 && git cherry-pick FETCH_HEAD
git fetch https://fuchsia.googlesource.com/third_party/jemalloc refs/changes/47/16947/2 && git cherry-pick FETCH_HEAD

git fetch https://fuchsia.googlesource.com/magma refs/changes/91/17091/3 && git cherry-pick FETCH_HEAD

git fetch https://fuchsia.googlesource.com/third_party/mesa refs/changes/56/16856/5 && git cherry-pick FETCH_HEAD
git fetch https://fuchsia.googlesource.com/third_party/mesa refs/changes/43/16943/3 && git cherry-pick FETCH_HEAD

2. Fetch cts dependencies

cd third_party/vulkan-cts
python external/fetch_sources.py

3. Create test list cases

NOTE: builds the test executable for the host in order to write out test cases

cd third_party/vulkancts
mkdir cases
python scripts/build_caselists.py cases

4. Create /data persistent storage filesystem on Acer internal ssd

NOTE: assumes you have a usb device at /dev/class/block/000

netruncmd magenta 'minfs /dev/class/block/001 mkfs'

then reboot

5. Build and copy binaries

magma/scripts/vulkancts/build.sh && magma/scripts/vulkancts/copy.sh

6. Execute

netruncmd magenta '/boot/bin/sh /data/vulkancts/run.sh'
