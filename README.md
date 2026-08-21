# libcpmfs

libcpmfs is a simple C library for manipulating CP/M filesystems.
It is intended to be used alongside a disk manipulation tool such as libhxcfe or
libdsk.

This is not made for data recovery on damaged disks.
If the superblock is corrupted, there will be errors.


## How to use

To interface with libcpmfs, you need to provide the following:
* Sector callback
* Filesystem attributes

The sector callback is needed by libcpmfs to request any given sector, addressed
in CHS (cylinder/head/sector). The sector number is the number relative to the
index pulse, starting at zero. Only the sector data is requested, without the
headers.

Filesystem attributes is a structure containing attributes relative to the type
of disk you're trying to read:
* Disk geometry
  * Cylinders: number of cylinders (tracks) per side, includes reserved ones.
  * Heads: number of heads. 1 or 2 on floppy disks, can be more on hard drives.
  * Sector count: number of sectors per cylinder.
  * Sector size: size of sector data (in bytes)
* CP/M attributes:
  * Block size: size of logical blocks. Cannot be smaller than a sector
  * Maximum directory entries: how many entries can fit on the disk
* Reserved cylinders (usually occupied by CP/M)
* Skew/Interleave: 
  * Skew Table: list of real sector numbers based on their position from index
  * Skew Factor: how many sectors between 2 consecutive numbers
    Skew table and factor are mutually exclusive
* Fill order: if the disk is filled in a non-standard order
              (e.g. side by side instead of cylinder by cylinder)

The `examples` directory contains a small implementation sample for reading a
directory and listing files. You can also check out
[libcpmfs-tools](https://github.com/Altomare/libcpmfs-tools),
which contain command line tools and tests using `libhxcfe` from
[HxCFloppyEmulator](https://github.com/jfdelnero/HxCFloppyEmulator)


## Limitations

At the moment, only CP/M 2.2 is supported and the library only allows sequential
file reading (seek is not implemented yet).

Tests have been done on little endian machines. It should work on big endian
machines but hasn't been tested yet.


## Licensing

Released under BSD 3-clause license. See `LICENSE`


## Some considerations

### CPM_ERR_FILE_OVERLAP

The `CPM_ERR_FILE_OVERLAP` error code indicates the disk has multiple file
entries pointing to the same block (one or more sectors). It's intended to
notify the user there's something wrong with the given disk geometry, as the
file table would have conflicts.

But there are cases where there might be two files pointing to the same blocks
intentionally. For instance, software can create a file pointing to bad blocks,
to reserve them and prevent the OS from writing to them. If a file was already
using such a block, we'll have two files pointing to the same block.

So far I only found a case with `[UNUSED].BAD` files, which list bad sectors via
the `FINDBAD` tool. These files won't cause the error, because the bracket
characters are considered to be an invalid filename.

It's still possible to encounter more of these scenarios in the wild. If so,
please do tell me so I can add workarounds. Alan R. Miller's book mentions
`BADLIM` and `RECLAIM` as tools that work like this, but I haven't tested them.

## Sector numbering and skew

There are many ways to order sectors on CP/M, which vary on a per-machine basis.
There's physical order on disk with numbers in the sector headers, a potential
translation done by the BIOS routines directly, and the sector translation table
contained in the BIOS and used by BDOS.

Some examples:
* Otrona Attaché:
  * Physical sectors are skewed/interleaved with a factor of 2 -> 1, 6, 2, 7...
  * The BIOS reading routine addresses addresses sectors by their number and the
    controller chip finds it
  * There's no translation table in the OS.
  * The interleave is solely done by the format command; it could be changed
    arbitrarily and the machine would still work
* Zorba:
  * Physical sectors are ordered on side 0 (1->10) & continue on side 1 (11->20)
  * The side number in sector headers is always zero
  * No translation table

As such, I chose to address sector by their position from the index pulse
instead of their ID. This means the skew factor from other disk definitions
such as 22disk or cpmtools might not fit. For instance the Otrona wouldn't
require any skew factor when addressing the sector by their number.

22disk solves that issue by inputting both the skew factor and the physical
sector order, but I chose to only keep one such setting (for now...).

## To-Do

Features:
 * OS version for different FS attributes. Only 2.x is supported now.
 * Support seek

Improvements:
 * Glossary, and check for inconsistent nomenclature (entry vs. extent for instance)

Testing:
 * Check for endianness bugs depending on the host endian.
   Might cause issue with 16-bit block addressing
 * Debug logs


## References

Books:
* CP/M 2.0 manual. [PDF](http://www.cpm.z80.de/manuals/SC-CPM.pdf)
* Mastering CP/M by Alan R. Miller. [PDF](https://oldcomputers.dyndns.org/public/pub/manuals/mastering_cpm.pdf)

Websites:
* [Seasip - John Elliott's website](https://www.seasip.info/Cpm/index.html)
* [DPB/DPH - sharpmz.org (mirror)](https://www.idealine.info/sharpmz/dpb.htm)

Software:
* [cpmtools 2.23](https://www.moria.de/~michael/cpmtools/), especially the manual pages
