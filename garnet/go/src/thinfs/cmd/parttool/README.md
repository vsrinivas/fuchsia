# parttool

A tool for creating & managing GPT partitions.

## Usage

```
parttool /dev/disk1 \
  EFI ESP 100m \
  SYS FuchsiaSystem 5g \
  DATA FuchsiaData 50% \
  BLOB FuchsiaBlob 50%
```

Size specifications accept k, m, and g for Kilobytes, Megabytes, Gigabytes
respectively. A size expressed as a percentage is calcuated as a percentage of
the volume *remaining* after constant sizes have been allocated.
