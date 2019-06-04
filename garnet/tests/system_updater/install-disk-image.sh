#!/boot/bin/sh

if [ -f /fake/install-disk-image-should-fail ]; then
    echo "The test asked for me to fail, so I will"
    exit 1
else
    echo "The test did not ask me to fail, so I won't"
    exit 0
fi