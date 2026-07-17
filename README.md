# libcpmfs

libcpmfs is a simple C library for manipulating CP/M filesystems.
It is intended to be used alongsize a disk manipulation util such as libhxcfe
or libdsk.

This is not made for data recovery on damaged disks.
If the superblock (basically the disk header) is corrupted, there will be errors.


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
  * Heads: number of heads. 1 or 2 on floppy disks, can be more on hdds.
  * Sector count: number of sectors per cylinder.
  * Sector size: size of sector data (in bytes)
* CP/M attributes:
  * Block size: size of logical blocks. Cannot be smaller than a sector
  * Maximum directory entries: how many entries can fit on the disk
* Reserved cylinders (usually occupied by CP/M)
* Skew table: if the sectors are interleaved, this contains the sector numbers
* Fill order: if the disk is filled in a non-standard order
              (eg. side by side instead of cylinder by cylinder)

The `examples` directory contains a small implementation sample for reading a
directory and listing files.


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
intentionally. For instance, an util might create a file pointing to bad blocks,
to reserve them and prevent the OS from writing to them. If a file was already
using such a block, we'll have two files pointing to the same block.

So far I only found a case with `[UNUSED].BAD` files, which list bad sectors via
the `FINDBAD` util. These files won't cause the error, because the bracket
characters are considered to be an invalid filename.

It's still possible to encounter more of these scenarios in the wild. If so,
please do tell me so I can add workarounds. Alan R. Miller's book mentions
`BADLIM` and `RECLAIM` as tools that work like this, but I haven't tested them.


## To-Do

Features:
 * OS version for different FS attributes. Only 2.x is supported now.
 * Logical to physical sector translation table (mostly for CP/M 1.4)
 * Support seek

Improvements:
 * Do not write the whole superblock at one, only the modified sectors
 * Add a NO_SYNC option where the superblock is only written when a sync()
   function is called by the user
 * More example code
 * Glossary, and check for inconsistent nomenclature (entry vs. extent for instance)

Testing:
 * Tests & examples
 * Check for endianness bugs depending on the host endian.
   Might cause issue with 16-bit block addressing
 * Gather many disk images for testing
 * Debug logs


## References

Books:
* CP/M 2.0 manual. [PDF](http://www.cpm.z80.de/manuals/SC-CPM.pdf)
* Mastering CP/M by Alan R. Miller. [OCR PDF](https://oldcomputers.dyndns.org/public/pub/manuals/mastering_cpm.pdf)

Websites:
* [Seasip - John Elliott's website](https://www.seasip.info/Cpm/index.html)
* [DPB/DPH - sharpmz.org (mirror)](https://www.idealine.info/sharpmz/dpb.htm)

Software:
* [cpmtools 2.23](https://www.moria.de/~michael/cpmtools/), especially the manual pages
