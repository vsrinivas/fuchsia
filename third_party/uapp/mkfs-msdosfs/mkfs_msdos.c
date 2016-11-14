/*
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mkfs_msdos.h"

#define	MAXU16	  0xffff	/* maximum unsigned 16-bit quantity */
#define	BPN	  4		/* bits per nibble */
#define	NPB	  2		/* nibbles per byte */

#define	DOSMAGIC  0xaa55	/* DOS magic number */
#define	MINBPS	  512		/* minimum bytes per sector */
#define	MAXSPC	  128		/* maximum sectors per cluster */
#define	MAXNFT	  16		/* maximum number of FATs */
#define	DEFBLK	  4096		/* default block size */
#define	DEFBLK16  2048		/* default block size FAT16 */
#define	DEFRDE	  512		/* default root directory entries */
#define	RESFTE	  2		/* reserved FAT entries */
#define	MINCLS12  1U		/* minimum FAT12 clusters */
#define	MINCLS16  0xff5U	/* minimum FAT16 clusters */
#define	MINCLS32  0xfff5U	/* minimum FAT32 clusters */
#define	MAXCLS12  0xff4U	/* maximum FAT12 clusters */
#define	MAXCLS16  0xfff4U	/* maximum FAT16 clusters */
#define	MAXCLS32  0xffffff4U	/* maximum FAT32 clusters */

#define	mincls(fat)  ((fat) == 12 ? MINCLS12 :	\
		      (fat) == 16 ? MINCLS16 :	\
				    MINCLS32)

#define	maxcls(fat)  ((fat) == 12 ? MAXCLS12 :	\
		      (fat) == 16 ? MAXCLS16 :	\
				    MAXCLS32)

#define	mk1(p, x)				\
    (p) = (uint8_t)(x)

#define	mk2(p, x)				\
    (p)[0] = (uint8_t)(x),			\
    (p)[1] = (uint8_t)((x) >> 010)

#define	mk4(p, x)				\
    (p)[0] = (uint8_t)(x),			\
    (p)[1] = (uint8_t)((x) >> 010),		\
    (p)[2] = (uint8_t)((x) >> 020),		\
    (p)[3] = (uint8_t)((x) >> 030)

struct bs {
    uint8_t bsJump[3];			/* bootstrap entry point */
    uint8_t bsOemName[8];		/* OEM name and version */
} __attribute__((packed));

struct bsbpb {
    uint8_t bpbBytesPerSec[2];		/* bytes per sector */
    uint8_t bpbSecPerClust;		/* sectors per cluster */
    uint8_t bpbResSectors[2];		/* reserved sectors */
    uint8_t bpbFATs;			/* number of FATs */
    uint8_t bpbRootDirEnts[2];		/* root directory entries */
    uint8_t bpbSectors[2];		/* total sectors */
    uint8_t bpbMedia;			/* media descriptor */
    uint8_t bpbFATsecs[2];		/* sectors per FAT */
    uint8_t bpbSecPerTrack[2];		/* sectors per track */
    uint8_t bpbHeads[2];		/* drive heads */
    uint8_t bpbHiddenSecs[4];		/* hidden sectors */
    uint8_t bpbHugeSectors[4];		/* big total sectors */
} __attribute__((packed));

struct bsxbpb {
    uint8_t bpbBigFATsecs[4];		/* big sectors per FAT */
    uint8_t bpbExtFlags[2];		/* FAT control flags */
    uint8_t bpbFSVers[2];		/* file system version */
    uint8_t bpbRootClust[4];		/* root directory start cluster */
    uint8_t bpbFSInfo[2];		/* file system info sector */
    uint8_t bpbBackup[2];		/* backup boot sector */
    uint8_t bpbReserved[12];		/* reserved */
} __attribute__((packed));

struct bsx {
    uint8_t exDriveNumber;		/* drive number */
    uint8_t exReserved1;		/* reserved */
    uint8_t exBootSignature;		/* extended boot signature */
    uint8_t exVolumeID[4];		/* volume ID number */
    uint8_t exVolumeLabel[11];		/* volume label */
    uint8_t exFileSysType[8];		/* file system type */
} __attribute__((packed));

struct de {
    uint8_t deName[11];		/* name and extension */
    uint8_t deAttributes;		/* attributes */
    uint8_t rsvd[10];			/* reserved */
    uint8_t deMTime[2];		/* last-modified time */
    uint8_t deMDate[2];		/* last-modified date */
    uint8_t deStartCluster[2];		/* starting cluster */
    uint8_t deFileSize[4];		/* size */
} __attribute__((packed));

struct bpb {
    uint32_t bpbBytesPerSec;		/* bytes per sector */
    uint32_t bpbSecPerClust;		/* sectors per cluster */
    uint32_t bpbResSectors;		/* reserved sectors */
    uint32_t bpbFATs;			/* number of FATs */
    uint32_t bpbRootDirEnts;		/* root directory entries */
    uint32_t bpbSectors;			/* total sectors */
    uint32_t bpbMedia;			/* media descriptor */
    uint32_t bpbFATsecs;			/* sectors per FAT */
    uint32_t bpbSecPerTrack;		/* sectors per track */
    uint32_t bpbHeads;			/* drive heads */
    uint32_t bpbHiddenSecs;		/* hidden sectors */
    uint32_t bpbHugeSectors; 		/* big total sectors */
    uint32_t bpbBigFATsecs; 		/* big sectors per FAT */
    uint32_t bpbRootClust; 		/* root directory start cluster */
    uint32_t bpbFSInfo; 			/* file system info sector */
    uint32_t bpbBackup; 			/* backup boot sector */
};

static const uint8_t bootcode[] = {
    0xfa,			/* cli		    */
    0x31, 0xc0, 		/* xor	   ax,ax    */
    0x8e, 0xd0, 		/* mov	   ss,ax    */
    0xbc, 0x00, 0x7c,		/* mov	   sp,7c00h */
    0xfb,			/* sti		    */
    0x8e, 0xd8, 		/* mov	   ds,ax    */
    0xe8, 0x00, 0x00,		/* call    $ + 3    */
    0x5e,			/* pop	   si	    */
    0x83, 0xc6, 0x19,		/* add	   si,+19h  */
    0xbb, 0x07, 0x00,		/* mov	   bx,0007h */
    0xfc,			/* cld		    */
    0xac,			/* lodsb	    */
    0x84, 0xc0, 		/* test    al,al    */
    0x74, 0x06, 		/* jz	   $ + 8    */
    0xb4, 0x0e, 		/* mov	   ah,0eh   */
    0xcd, 0x10, 		/* int	   10h	    */
    0xeb, 0xf5, 		/* jmp	   $ - 9    */
    0x30, 0xe4, 		/* xor	   ah,ah    */
    0xcd, 0x16, 		/* int	   16h	    */
    0xcd, 0x19, 		/* int	   19h	    */
    0x0d, 0x0a,
    'N', 'o', 'n', '-', 's', 'y', 's', 't',
    'e', 'm', ' ', 'd', 'i', 's', 'k',
    0x0d, 0x0a,
    'P', 'r', 'e', 's', 's', ' ', 'a', 'n',
    'y', ' ', 'k', 'e', 'y', ' ', 't', 'o',
    ' ', 'r', 'e', 'b', 'o', 'o', 't',
    0x0d, 0x0a,
    0
};

static int getdiskinfo(int, const char *, struct bpb *);
static void print_bpb(struct bpb *);
static int ckgeom(const char *, uint32_t, const char *);
static void mklabel(uint8_t *, const char *);
static int oklabel(const char *);
static void setstr(uint8_t *, const char *, size_t);

int
mkfs_msdos(const char *fname, const struct msdos_options *op)
{
    char buf[MAXPATHLEN];
    struct stat sb;
    struct timeval tv;
    struct bpb bpb;
    struct tm *tm;
    struct bs *bs;
    struct bsbpb *bsbpb;
    struct bsxbpb *bsxbpb;
    struct bsx *bsx;
    struct de *de;
    uint8_t *img;
    const char *bname;
    ssize_t n;
    time_t now;
    uint32_t fat, bss, rds, cls, dir, lsn, x, x1, x2;
    int fd, fd1, rv;
    struct msdos_options o = *op;

    img = NULL;
    rv = -1;

    if (o.block_size && o.sectors_per_cluster) {
	warnx("Cannot specify both block size and sectors per cluster");
	goto done;
    }
    if (o.OEM_string && strlen(o.OEM_string) > 8) {
	warnx("%s: bad OEM string", o.OEM_string);
	goto done;
    }
    if (o.create_size) {
	if (o.no_create) {
	    warnx("create (-C) is incompatible with -N");
	    goto done;
	}
	fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
	    warnx("failed to create %s", fname);
	    goto done;
	}
	if (ftruncate(fd, o.create_size)) {
	    warnx("failed to initialize %jd bytes", (intmax_t)o.create_size);
	    goto done;
	}
    } else if ((fd = open(fname, o.no_create ? O_RDONLY : O_RDWR)) == -1) {
	warn("%s", fname);
	goto done;
    }
    if (fstat(fd, &sb)) {
	warn("%s", fname);
	goto done;
    }
    if (o.create_size) {
	if (!S_ISREG(sb.st_mode))
	    warnx("warning, %s is not a regular file", fname);
    } else {
	if (!S_ISCHR(sb.st_mode))
	    warnx("warning, %s is not a character device", fname);
    }
    if (o.offset && o.offset != lseek(fd, o.offset, SEEK_SET)) {
	warnx("cannot seek to %jd", (intmax_t)o.offset);
	goto done;
    }
    memset(&bpb, 0, sizeof(bpb));
    getdiskinfo(fd, fname, &bpb);
    bpb.bpbHugeSectors -= (o.offset / bpb.bpbBytesPerSec);
    if (bpb.bpbSecPerClust == 0) {	/* set defaults */
        if (bpb.bpbHugeSectors <= 6000)	/* about 3MB -> 512 bytes */
    	bpb.bpbSecPerClust = 1;
        else if (bpb.bpbHugeSectors <= (1<<17)) /* 64M -> 4k */
    	bpb.bpbSecPerClust = 8;
        else if (bpb.bpbHugeSectors <= (1<<19)) /* 256M -> 8k */
    	bpb.bpbSecPerClust = 16;
        else if (bpb.bpbHugeSectors <= (1<<21)) /* 1G -> 16k */
    	bpb.bpbSecPerClust = 32;
        else
    	bpb.bpbSecPerClust = 64;		/* otherwise 32k */
    }
    if (!powerof2(bpb.bpbBytesPerSec)) {
	warnx("bytes/sector (%u) is not a power of 2", bpb.bpbBytesPerSec);
	goto done;
    }
    if (bpb.bpbBytesPerSec < MINBPS) {
	warnx("bytes/sector (%u) is too small; minimum is %u",
	     bpb.bpbBytesPerSec, MINBPS);
	goto done;
    }

    if (o.volume_label && !oklabel(o.volume_label)) {
	warnx("%s: bad volume label", o.volume_label);
	goto done;
    }
    if (!(fat = o.fat_type)) {
	if (!o.directory_entries && (o.info_sector || o.backup_sector))
	    fat = 32;
    }
    if ((fat == 32 && o.directory_entries) || (fat != 32 && (o.info_sector || o.backup_sector))) {
	warnx("-%c is not a legal FAT%s option",
	     fat == 32 ? 'e' : o.info_sector ? 'i' : 'k',
	     fat == 32 ? "32" : "12/16");
	goto done;
    }
    if (fat != 0 && fat != 12 && fat != 16 && fat != 32) {
	warnx("%d: bad FAT type", fat);
	goto done;
    }

    if (o.block_size) {
	if (!powerof2(o.block_size)) {
	    warnx("block size (%u) is not a power of 2", o.block_size);
	    goto done;
	}
	if (o.block_size < bpb.bpbBytesPerSec) {
	    warnx("block size (%u) is too small; minimum is %u",
		 o.block_size, bpb.bpbBytesPerSec);
	    goto done;
	}
	if (o.block_size > bpb.bpbBytesPerSec * MAXSPC) {
	    warnx("block size (%u) is too large; maximum is %u",
		 o.block_size, bpb.bpbBytesPerSec * MAXSPC);
	    goto done;
	}
	bpb.bpbSecPerClust = o.block_size / bpb.bpbBytesPerSec;
    }
    if (o.sectors_per_cluster) {
	if (!powerof2(o.sectors_per_cluster)) {
	    warnx("sectors/cluster (%u) is not a power of 2",
		o.sectors_per_cluster);
	    goto done;
	}
	bpb.bpbSecPerClust = o.sectors_per_cluster;
    }
    if (o.reserved_sectors)
	bpb.bpbResSectors = o.reserved_sectors;
    if (o.num_FAT) {
	if (o.num_FAT > MAXNFT) {
	    warnx("number of FATs (%u) is too large; maximum is %u",
		 o.num_FAT, MAXNFT);
	    goto done;
	}
	bpb.bpbFATs = o.num_FAT;
    }
    if (o.directory_entries)
	bpb.bpbRootDirEnts = o.directory_entries;
    if (o.media_descriptor_set) {
	if (o.media_descriptor < 0xf0) {
	    warnx("illegal media descriptor (%#x)", o.media_descriptor);
	    goto done;
	}
	bpb.bpbMedia = o.media_descriptor;
    }
    if (o.sectors_per_fat)
	bpb.bpbBigFATsecs = o.sectors_per_fat;
    if (o.info_sector)
	bpb.bpbFSInfo = o.info_sector;
    if (o.backup_sector)
	bpb.bpbBackup = o.backup_sector;
    bss = 1;
    bname = NULL;
    fd1 = -1;
    if (o.bootstrap) {
	bname = o.bootstrap;
	if (!strchr(bname, '/')) {
	    snprintf(buf, sizeof(buf), "/boot/%s", bname);
	    if (!(bname = strdup(buf))) {
		warn(NULL);
		goto done;
	    }
	}
	if ((fd1 = open(bname, O_RDONLY)) == -1 || fstat(fd1, &sb)) {
	    warn("%s", bname);
	    goto done;
	}
	if (!S_ISREG(sb.st_mode) || sb.st_size % bpb.bpbBytesPerSec ||
	    sb.st_size < bpb.bpbBytesPerSec ||
	    sb.st_size > bpb.bpbBytesPerSec * MAXU16) {
	    warnx("%s: inappropriate file type or format", bname);
	    goto done;
	}
	bss = sb.st_size / bpb.bpbBytesPerSec;
    }
    if (!bpb.bpbFATs)
	bpb.bpbFATs = 2;
    if (!fat) {
	if (bpb.bpbHugeSectors < (bpb.bpbResSectors ? bpb.bpbResSectors : bss) +
	    howmany((RESFTE + (bpb.bpbSecPerClust ? MINCLS16 : MAXCLS12 + 1)) *
		(bpb.bpbSecPerClust ? 16 : 12) / BPN,
		bpb.bpbBytesPerSec * NPB) *
	    bpb.bpbFATs +
	    howmany(bpb.bpbRootDirEnts ? bpb.bpbRootDirEnts : DEFRDE,
		    bpb.bpbBytesPerSec / sizeof(struct de)) +
	    (bpb.bpbSecPerClust ? MINCLS16 : MAXCLS12 + 1) *
	    (bpb.bpbSecPerClust ? bpb.bpbSecPerClust :
	     howmany(DEFBLK, bpb.bpbBytesPerSec)))
	    fat = 12;
	else if (bpb.bpbRootDirEnts || bpb.bpbHugeSectors <
		 (bpb.bpbResSectors ? bpb.bpbResSectors : bss) +
		 howmany((RESFTE + MAXCLS16) * 2, bpb.bpbBytesPerSec) *
		 bpb.bpbFATs +
		 howmany(DEFRDE, bpb.bpbBytesPerSec / sizeof(struct de)) +
		 (MAXCLS16 + 1) *
		 (bpb.bpbSecPerClust ? bpb.bpbSecPerClust :
		  howmany(8192, bpb.bpbBytesPerSec)))
	    fat = 16;
	else
	    fat = 32;
    }
    x = bss;
    if (fat == 32) {
	if (!bpb.bpbFSInfo) {
	    if (x == MAXU16 || x == bpb.bpbBackup) {
		warnx("no room for info sector");
		goto done;
	    }
	    bpb.bpbFSInfo = x;
	}
	if (bpb.bpbFSInfo != MAXU16 && x <= bpb.bpbFSInfo)
	    x = bpb.bpbFSInfo + 1;
	if (!bpb.bpbBackup) {
	    if (x == MAXU16) {
		warnx("no room for backup sector");
		goto done;
	    }
	    bpb.bpbBackup = x;
	} else if (bpb.bpbBackup != MAXU16 && bpb.bpbBackup == bpb.bpbFSInfo) {
	    warnx("backup sector would overwrite info sector");
	    goto done;
	}
	if (bpb.bpbBackup != MAXU16 && x <= bpb.bpbBackup)
	    x = bpb.bpbBackup + 1;
    }
    if (!bpb.bpbResSectors)
	bpb.bpbResSectors = fat == 32 ?
	    MAX(x, MAX(16384 / bpb.bpbBytesPerSec, 4)) : x;
    else if (bpb.bpbResSectors < x) {
	warnx("too few reserved sectors (need %d have %d)", x,
	     bpb.bpbResSectors);
	goto done;
    }
    if (fat != 32 && !bpb.bpbRootDirEnts)
	bpb.bpbRootDirEnts = DEFRDE;
    rds = howmany(bpb.bpbRootDirEnts, bpb.bpbBytesPerSec / sizeof(struct de));
    if (!bpb.bpbSecPerClust)
	for (bpb.bpbSecPerClust = howmany(fat == 16 ? DEFBLK16 :
					  DEFBLK, bpb.bpbBytesPerSec);
	     bpb.bpbSecPerClust < MAXSPC &&
	     bpb.bpbResSectors +
	     howmany((RESFTE + maxcls(fat)) * (fat / BPN),
		     bpb.bpbBytesPerSec * NPB) *
	     bpb.bpbFATs +
	     rds +
	     (uint64_t) (maxcls(fat) + 1) *
	     bpb.bpbSecPerClust <= bpb.bpbHugeSectors;
	     bpb.bpbSecPerClust <<= 1)
	    continue;
    if (fat != 32 && bpb.bpbBigFATsecs > MAXU16) {
	warnx("too many sectors/FAT for FAT12/16");
	goto done;
    }
    x1 = bpb.bpbResSectors + rds;
    x = bpb.bpbBigFATsecs ? bpb.bpbBigFATsecs : 1;
    if (x1 + (uint64_t)x * bpb.bpbFATs > bpb.bpbHugeSectors) {
	warnx("meta data exceeds file system size");
	goto done;
    }
    x1 += x * bpb.bpbFATs;
    x = (uint64_t)(bpb.bpbHugeSectors - x1) * bpb.bpbBytesPerSec * NPB /
	(bpb.bpbSecPerClust * bpb.bpbBytesPerSec * NPB + fat /
	 BPN * bpb.bpbFATs);
    x2 = howmany((RESFTE + MIN(x, maxcls(fat))) * (fat / BPN),
		 bpb.bpbBytesPerSec * NPB);
    if (!bpb.bpbBigFATsecs) {
	bpb.bpbBigFATsecs = x2;
	x1 += (bpb.bpbBigFATsecs - 1) * bpb.bpbFATs;
    }
    cls = (bpb.bpbHugeSectors - x1) / bpb.bpbSecPerClust;
    x = (uint64_t)bpb.bpbBigFATsecs * bpb.bpbBytesPerSec * NPB / (fat / BPN) -
	RESFTE;
    if (cls > x)
	cls = x;
    if (bpb.bpbBigFATsecs < x2)
	warnx("warning: sectors/FAT limits file system to %u clusters",
	      cls);
    if (cls < mincls(fat)) {
	warnx("%u clusters too few clusters for FAT%u, need %u", cls, fat,
	    mincls(fat));
	goto done;
    }
    if (cls > maxcls(fat)) {
	cls = maxcls(fat);
	bpb.bpbHugeSectors = x1 + (cls + 1) * bpb.bpbSecPerClust - 1;
	warnx("warning: FAT type limits file system to %u sectors",
	      bpb.bpbHugeSectors);
    }
    printf("%s: %u sector%s in %u FAT%u cluster%s "
	   "(%u bytes/cluster)\n", fname, cls * bpb.bpbSecPerClust,
	   cls * bpb.bpbSecPerClust == 1 ? "" : "s", cls, fat,
	   cls == 1 ? "" : "s", bpb.bpbBytesPerSec * bpb.bpbSecPerClust);
    if (!bpb.bpbMedia)
	bpb.bpbMedia = !bpb.bpbHiddenSecs ? 0xf0 : 0xf8;
    if (fat == 32)
	bpb.bpbRootClust = RESFTE;
    if (bpb.bpbHugeSectors <= MAXU16) {
	bpb.bpbSectors = bpb.bpbHugeSectors;
	bpb.bpbHugeSectors = 0;
    }
    if (fat != 32) {
	bpb.bpbFATsecs = bpb.bpbBigFATsecs;
	bpb.bpbBigFATsecs = 0;
    }
    print_bpb(&bpb);
    if (!o.no_create) {
	gettimeofday(&tv, NULL);
	now = tv.tv_sec;
	tm = localtime(&now);
	if (!(img = malloc(bpb.bpbBytesPerSec))) {
	    warn(NULL);
	    goto done;
	}
	dir = bpb.bpbResSectors + (bpb.bpbFATsecs ? bpb.bpbFATsecs :
				   bpb.bpbBigFATsecs) * bpb.bpbFATs;
	for (lsn = 0; lsn < dir + (fat == 32 ? bpb.bpbSecPerClust : rds); lsn++) {
	    x = lsn;
	    if (o.bootstrap &&
		fat == 32 && bpb.bpbBackup != MAXU16 &&
		bss <= bpb.bpbBackup && x >= bpb.bpbBackup) {
		x -= bpb.bpbBackup;
		if (!x && lseek(fd1, o.offset, SEEK_SET)) {
		    warn("%s", bname);
		    goto done;
		}
	    }
	    if (o.bootstrap && x < bss) {
		if ((n = read(fd1, img, bpb.bpbBytesPerSec)) == -1) {
		    warn("%s", bname);
		    goto done;
		}
		if ((unsigned)n != bpb.bpbBytesPerSec) {
		    warnx("%s: can't read sector %u", bname, x);
		    goto done;
		}
	    } else
		memset(img, 0, bpb.bpbBytesPerSec);
	    if (!lsn ||
		(fat == 32 && bpb.bpbBackup != MAXU16 &&
		 lsn == bpb.bpbBackup)) {
		x1 = sizeof(struct bs);
		bsbpb = (struct bsbpb *)(img + x1);
		mk2(bsbpb->bpbBytesPerSec, bpb.bpbBytesPerSec);
		mk1(bsbpb->bpbSecPerClust, bpb.bpbSecPerClust);
		mk2(bsbpb->bpbResSectors, bpb.bpbResSectors);
		mk1(bsbpb->bpbFATs, bpb.bpbFATs);
		mk2(bsbpb->bpbRootDirEnts, bpb.bpbRootDirEnts);
		mk2(bsbpb->bpbSectors, bpb.bpbSectors);
		mk1(bsbpb->bpbMedia, bpb.bpbMedia);
		mk2(bsbpb->bpbFATsecs, bpb.bpbFATsecs);
		mk2(bsbpb->bpbSecPerTrack, bpb.bpbSecPerTrack);
		mk2(bsbpb->bpbHeads, bpb.bpbHeads);
		mk4(bsbpb->bpbHiddenSecs, bpb.bpbHiddenSecs);
		mk4(bsbpb->bpbHugeSectors, bpb.bpbHugeSectors);
		x1 += sizeof(struct bsbpb);
		if (fat == 32) {
		    bsxbpb = (struct bsxbpb *)(img + x1);
		    mk4(bsxbpb->bpbBigFATsecs, bpb.bpbBigFATsecs);
		    mk2(bsxbpb->bpbExtFlags, 0);
		    mk2(bsxbpb->bpbFSVers, 0);
		    mk4(bsxbpb->bpbRootClust, bpb.bpbRootClust);
		    mk2(bsxbpb->bpbFSInfo, bpb.bpbFSInfo);
		    mk2(bsxbpb->bpbBackup, bpb.bpbBackup);
		    x1 += sizeof(struct bsxbpb);
		}
		bsx = (struct bsx *)(img + x1);
		mk1(bsx->exDriveNumber, 0x80);
		mk1(bsx->exBootSignature, 0x29);
		if (o.volume_id_set)
		    x = o.volume_id;
		else
		    x = (((uint32_t)(1 + tm->tm_mon) << 8 |
			  (uint32_t)tm->tm_mday) +
			 ((uint32_t)tm->tm_sec << 8 |
			  (uint32_t)(tv.tv_usec / 10))) << 16 |
			((uint32_t)(1900 + tm->tm_year) +
			 ((uint32_t)tm->tm_hour << 8 |
			  (uint32_t)tm->tm_min));
		mk4(bsx->exVolumeID, x);
		mklabel(bsx->exVolumeLabel, o.volume_label ? o.volume_label : "NO NAME");
		snprintf(buf, sizeof(buf), "FAT%u", fat);
		setstr(bsx->exFileSysType, buf, sizeof(bsx->exFileSysType));
		if (!o.bootstrap) {
		    x1 += sizeof(struct bsx);
		    bs = (struct bs *)img;
		    mk1(bs->bsJump[0], 0xeb);
		    mk1(bs->bsJump[1], x1 - 2);
		    mk1(bs->bsJump[2], 0x90);
		    setstr(bs->bsOemName, o.OEM_string ? o.OEM_string : "BSD4.4  ",
			   sizeof(bs->bsOemName));
		    memcpy(img + x1, bootcode, sizeof(bootcode));
		    mk2(img + MINBPS - 2, DOSMAGIC);
		}
	    } else if (fat == 32 && bpb.bpbFSInfo != MAXU16 &&
		       (lsn == bpb.bpbFSInfo ||
			(bpb.bpbBackup != MAXU16 &&
			 lsn == bpb.bpbBackup + bpb.bpbFSInfo))) {
		mk4(img, 0x41615252);
		mk4(img + MINBPS - 28, 0x61417272);
		mk4(img + MINBPS - 24, 0xffffffff);
		mk4(img + MINBPS - 20, bpb.bpbRootClust);
		mk2(img + MINBPS - 2, DOSMAGIC);
	    } else if (lsn >= bpb.bpbResSectors && lsn < dir &&
		       !((lsn - bpb.bpbResSectors) %
			 (bpb.bpbFATsecs ? bpb.bpbFATsecs :
			  bpb.bpbBigFATsecs))) {
		mk1(img[0], bpb.bpbMedia);
		for (x = 1; x < fat * (fat == 32 ? 3 : 2) / 8; x++)
		    mk1(img[x], fat == 32 && x % 4 == 3 ? 0x0f : 0xff);
	    } else if (lsn == dir && o.volume_label) {
		de = (struct de *)img;
		mklabel(de->deName, o.volume_label);
		mk1(de->deAttributes, 050);
		x = (uint32_t)tm->tm_hour << 11 |
		    (uint32_t)tm->tm_min << 5 |
		    (uint32_t)tm->tm_sec >> 1;
		mk2(de->deMTime, x);
		x = (uint32_t)(tm->tm_year - 80) << 9 |
		    (uint32_t)(tm->tm_mon + 1) << 5 |
		    (uint32_t)tm->tm_mday;
		mk2(de->deMDate, x);
	    }
	    if ((n = write(fd, img, bpb.bpbBytesPerSec)) == -1) {
		warn("%s", fname);
		goto done;
	    }
	    if ((unsigned)n != bpb.bpbBytesPerSec) {
		warnx("%s: can't write sector %u", fname, lsn);
		goto done;
	    }
	}
    }
    rv = 0;
done:
    free(img);

    return rv;
}

/*
 * Get disk slice, partition, and geometry information.
 */
static int
getdiskinfo(int fd, const char *fname, struct bpb *bpb)
{
    off_t ms = 0;

    struct stat st;
    if (fstat(fd, &st))
        err(1, "cannot get disk size");
    /* create a fake geometry for a file image */
    ms = st.st_size;
    bpb->bpbBytesPerSec = 512;
    bpb->bpbSecPerTrack = 63;
    bpb->bpbHeads = 255;
    bpb->bpbHugeSectors = ms / bpb->bpbBytesPerSec;
    bpb->bpbHiddenSecs = 0;
    return 0;
}

/*
 * Print out BPB values.
 */
static void
print_bpb(struct bpb *bpb)
{
    printf("BytesPerSec=%u SecPerClust=%u ResSectors=%u FATs=%u",
	   bpb->bpbBytesPerSec, bpb->bpbSecPerClust, bpb->bpbResSectors,
	   bpb->bpbFATs);
    if (bpb->bpbRootDirEnts)
	printf(" RootDirEnts=%u", bpb->bpbRootDirEnts);
    if (bpb->bpbSectors)
	printf(" Sectors=%u", bpb->bpbSectors);
    printf(" Media=%#x", bpb->bpbMedia);
    if (bpb->bpbFATsecs)
	printf(" FATsecs=%u", bpb->bpbFATsecs);
    printf(" SecPerTrack=%u Heads=%u HiddenSecs=%u", bpb->bpbSecPerTrack,
	   bpb->bpbHeads, bpb->bpbHiddenSecs);
    if (bpb->bpbHugeSectors)
	printf(" HugeSectors=%u", bpb->bpbHugeSectors);
    if (!bpb->bpbFATsecs) {
	printf(" FATsecs=%u RootCluster=%u", bpb->bpbBigFATsecs,
	       bpb->bpbRootClust);
	printf(" FSInfo=");
	printf(bpb->bpbFSInfo == MAXU16 ? "%#x" : "%u", bpb->bpbFSInfo);
	printf(" Backup=");
	printf(bpb->bpbBackup == MAXU16 ? "%#x" : "%u", bpb->bpbBackup);
    }
    printf("\n");
}

/*
 * Check a disk geometry value.
 */
static int
ckgeom(const char *fname, uint32_t val, const char *msg)
{
    if (!val) {
	warnx("%s: no default %s", fname, msg);
	return -1;
    }
    if (val > MAXU16) {
	warnx("%s: illegal %s %d", fname, msg, val);
	return -1;
    }
    return 0;
}

/*
 * Check a volume label.
 */
static int
oklabel(const char *src)
{
    int c, i;

    for (i = 0; i <= 11; i++) {
	c = (u_char)*src++;
	if (c < ' ' + !i || strchr("\"*+,./:;<=>?[\\]|", c))
	    break;
    }
    return i && !c;
}

/*
 * Make a volume label.
 */
static void
mklabel(uint8_t *dest, const char *src)
{
    int c, i;

    for (i = 0; i < 11; i++) {
	c = *src ? toupper(*src++) : ' ';
	*dest++ = !i && c == '\xe5' ? 5 : c;
    }
}

/*
 * Copy string, padding with spaces.
 */
static void
setstr(uint8_t *dest, const char *src, size_t len)
{
    while (len--)
	*dest++ = *src ? *src++ : ' ';
}
