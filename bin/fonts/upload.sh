#!/bin/sh

set -e

print_usage() {
    echo "Usage: $0 <font_dir_to_upload>" >&2
}

if [ "$#" -ne 1 ] || ! [ -d "$1" ]; then
    print_usage
    exit 1
fi

FONT_DIR=${1}
BASE_NAME=$(basename ${FONT_DIR})
ARCHIVE="${FONT_DIR}.tar.bz2"
FILES=$(ls -C ${FONT_DIR})
tar -cjf ${ARCHIVE} -C ${FONT_DIR} ${FILES}

SHA1=$(sha1sum ${ARCHIVE} | awk '{print $1}')

GS_PATH="fuchsia-build/fuchsia/fonts/${BASE_NAME}/${SHA1}"
gsutil cp ${ARCHIVE} "gs://${GS_PATH}"
echo "https://storage.googleapis.com/${GS_PATH}" > ${FONT_DIR}.version