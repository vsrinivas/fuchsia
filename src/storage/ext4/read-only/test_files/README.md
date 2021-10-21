# Ext4 Test Files

These are the ext4 test images we use to test the capability of the reader.

**Note:** There are many flags/properties, and it would be too verbose to be
exhaustive. Only notable properties will be mentioned.

## 1file.img

The most basic, just has one file in the root directory.

### Properties

* `1 KiB` Block Size
* Single block group

#### Flags

* 32 bit

### Directory Tree

```
/
├── file1
└── lost+found/
```

## extents.img

Contains large files with extent depth > 0 and sparse files and multiple
nested directories.

### Properties

* `1 KiB` Block Size
* 2 block groups
* largefile (inode 14) has extent tree with depth 2
* sparsefile (inode 12) has extent tree with depth 2, logical blocks are not contiguous

#### Flags

* 32 bit
* Large files
* Huge files

### Directory Tree

```
/
├── a/
│   └── multi/
│       └── dir/
│           └── path/
│               └── within/
│                   └── this/
│                       └── crowded/
│                           └── extents/
│                               └── test/
│                                   └── img/
│                                       └── empty
├── largefile
├── lost+found/
├── smallfile
└── sparsefile
```

## nest.img

Test that we can walk into a sub-directory.

### Properties

* `1 KiB` Block Size
* Single block group

#### Flags

* 32 bit

### Directory Tree

```
/
├── file1
├── inner/
│   └── file2
└── lost+found/
```
