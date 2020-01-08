#!/bin/sh

ZBI=""
IMAGE=""
OUTFILE=""

while [ $# -gt 0 ]; do
  case $1 in
    -z)
      ZBI="$2"
      shift
      shift
      ;;
    -i)
      IMAGE="$2"
      shift
      shift
      ;;
    -o)
      OUTFILE="$2"
      shift
      shift
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "Usage: $0 -z <zbi> -i <image> -o <outfile> -- <PATTERN> [<PATTERN>...]"
      echo "  -z: zbi executable"
      echo "  -i: filesystem image"
      echo "  -o: file to create on success"
      return 1
      ;;
  esac
done

OUTPUT="$("$ZBI" -tv "$IMAGE")"
CMDLINE="${OUTPUT##*CMDLINE}"

rm -f $OUTFILE

for PATTERN in "$@"; do
  case "$CMDLINE" in
    *"$PATTERN"*)
      continue
      ;;
    *)
      echo "Pattern \"$PATTERN\" not found in command line: $CMDLINE"
      exit 1
      ;;
  esac
done

touch $OUTFILE
