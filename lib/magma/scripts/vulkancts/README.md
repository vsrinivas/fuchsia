1. Fetch cts dependencies

cd third_party/vulkan-cts
python external/fetch_sources.py

2. Ensure cmake is in your path

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

5. Make sure netstack is running

netstack&

6. Execute

netruncmd : '/data/vulkancts/run.sh'
