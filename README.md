# libcpmfs

libcpmfs is a simple C library for reading CP/M filesystems.
It is intended to be used alongside disk manipulation software.

This is not made for data recovery on damaged disks.
If there are errors in the superblock, there will be read errors.

## How to use

To interface with libcpmfs, you need to provide the following:
* Sector callback
* Filesystem attributes

The sector callback is a way for libcpmfs to request any given sector, addressed
in CHS (cylinder/head/sector). Only the sector data is requested, without
the headers.

Filesystem attributes is a structure containing attributes relative to the type
of disk you're trying to read:
* Disk geometry
  * Cylinders: number of cylinders (tracks) per side, includes reserved ones.
  * Heads: number of heads. 1 or 2 on floppy disks, can be more on hdds.
  * Sector count: number of sectors per cylinder.
  * Sector size: size of sector data (in bytes)
* CP/M attributes:
  * Block size: size of logical blocks. Cannot be smaller than a sector
  * Maximum directory entries: how many entries can fit on the disk
* Reserved cylinders (usually occupied by CP/M)

The `examples` directory contains a small implementation sample for reading a
directory and listing files.

## Limitations

At the moment, only CP/M 2.2 is supported.

There is no way to create or modify files.

Tests have been done on little endian machines. It should work on big endian
machines but hasn't been tested yet.

## Licensing

Released under BSD 3-clause license. See `LICENSE`
