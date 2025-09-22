# libcpmfs

libcpmfs is a simple C library meant to manipulate CP/M filesystems.
It is intended to be used alongsize a disk manipulation util.

This is not made for data recovery on damaged disks.
If there are errors in the superblock, there will be read errors.

At the moment, only CP/M 2.2 is supported and the library only allows sequential file reading.

## Licensing

Released under BSD 3-clause license. See `LICENSE`

## To-Do

 * OS version for different FS attributes. Only 2.2 is supported now.
 * Debug logs
 * Check for endianness bugs depending on the host endian. Might cause issue with 16-bit block addressing
 * Glossary, and check for inconsistent nomenclature
 * Tests & examples
 * Support seek, write, unlink...
