/* Copyright (c) 2025 Arthur DAUZAT
 * SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "libcpmfs.h"

/* File entry macros */
#define F_IS_READONLY(entry) (entry->extension[0] & 0x80)
#define F_IS_SYSTEMFILE(entry) (entry->extension[1] & 0x80)
#define F_IS_ARCHIVED(entry) (entry->extension[2] & 0x80)

#define F_SET_READONLY(entry) entry->extension[0] |= 0x80
#define F_SET_SYSTEMFILE(entry) entry->extension[1] |= 0x80
#define F_SET_ARCHIVED(entry) entry->extension[2] |= 0x80

/* If the disk has less than 256 available blocks (including directory area),
 * block_ptr is interpreted as 16 integers. Otherwise it's 8 2-byte values. */
enum cpm_fs_block_addressing {
	CPM_BLOCK_ADDR_8, /* 8 bit block pointers */
	CPM_BLOCK_ADDR_16, /* 16 bit block pointers */
};

/* To prevent confusion, we will use "entry" to designate physical extents,
 * and "extent" for logical ones (16k).
 * As a reminder, a physical extent can contain one or more logical one,
 * depending on the disk geometry. */

/* Directory Entry = Physical Extent */
typedef struct {
	/* Status: 0-15 = user number. 0xE5 = unused.
	 * CP/M 1.4 used 0x80 for hidden files, but this was deprecated. */
	uint8_t status;
	uint8_t file[8];
	/* File ext in ASCII, high bits are used for status */
	uint8_t extension[3];
	/* Extent Number (low). Bits 5 to 7 are set to zero. */
	uint8_t extent_l;
	/* Byte Count. 3.1 upwards */
	uint8_t bc;
	/* Extent Number (high). Bits 6 and 7 are set to zero. 2.2 upwards. */
	uint8_t extent_h;
	/* Record Count: number of 128-byte records in last logical extent. */
	uint8_t rc;
	union {
		uint8_t block_ptr[16];
		uint16_t block_ptr_w[8];
	};
} __attribute__((packed, aligned(1))) cpm_entry;

/* The extent number given by excent_l/_h corresponds to the highest logical
 * extent in the current entry. An entry can have multiple logical 16k extents.
 * In all cases, rc only refers to the last logical extent in that entry. */

struct cpm_superblock {
	uint32_t count;
	cpm_entry *entries;
};

struct cpm_fs_file_handle {
	uint32_t entry;
	uint32_t block; /* Current block index in the physical extent */
	uint32_t offset; /* Offset in current block */
	enum cpm_fs_mode mode;
};

struct cpm_fs_dir {
	struct cpm_fs *fs;
	struct cpm_fs_file file;
	int32_t current_file_ino;
};

struct cpm_fs {
	struct cpm_fs_attr attr;
	struct cpm_superblock superblock;

	/* Block allocation vector. One bit per block. */
	uint8_t *av;

	/* Total available size in bytes for files & superblock,
	 * without skipped tracks */
	uint32_t disk_size;
	enum cpm_fs_block_addressing block_addressing;

	/* Cache for one sector */
	uint8_t *cache;

	read_sector_cb read_sector;
	write_sector_cb write_sector;
	void *userdata;
};


/* --- Disk utils ------------------------------------------------------ */

/* Get disk size available for files, in bytes.
 * Reserved tracks excluded, superblock included */
uint32_t get_disk_size(struct cpm_fs *fs);
void block_to_chs(struct cpm_fs *fs,
		  uint32_t block,
		  uint32_t block_offset,
		  uint32_t *c,
		  uint32_t *h,
		  uint32_t *s);
uint32_t apply_skew(struct cpm_fs *fs, uint32_t sector);

/* --- Allocation vector ----------------------------------------------- */

/* 0 on success, negative status on error */
int av_build(struct cpm_fs *fs);

void av_set(struct cpm_fs *fs, int block_index);
void av_unset(struct cpm_fs *fs, int block_index);
int av_get(struct cpm_fs *fs, int block_index);

/* --- Validity checks ------------------------------------------------- */

/* Check for superblock validity, returns 0 or negative error code */
int check_superblock(struct cpm_fs *fs);

/* Return true if the given character is accepted for filenames */
bool is_allowed_char(char c);

/* Return true if the user number is valid */
bool is_valid_user(int user);

/* Return true if the given cpm entry is valid */
bool cpm_entry_is_valid(const cpm_entry *entry);

/* --- Files ----------------------------------------------------------- */

/* Create a new file entry, returns 0 or negative error code. */
int create_file(struct cpm_fs *fs, const char *pathname, int user);

/* Return first entry (lowest extent number) for given path, -1 on error. */
int32_t find_file(struct cpm_fs *fs, const char *pathname, int user);

/* Get filesize in bytes for given entry */
uint32_t get_filesize(struct cpm_fs *fs, cpm_entry *entry);

int rename_file(const char *old_path,
		int old_user,
		const char *new_path,
		int new_user);

/* Check path validity and extract parts (spaces included).
 * Return 0 on success or negative error code. */
int parse_filename(const char *pathname,
		   char **out_file,
		   size_t *out_filelen,
		   char **out_ext,
		   size_t *out_extlen);

/* --- Extents---------------------------------------------------------- */

/* Physical extents / directory entries */
int alloc_new_extent(struct cpm_fs *fs, cpm_entry *src_entry);
uint32_t get_next_extent(struct cpm_fs *fs, uint32_t extent);
void wipe_extent(struct cpm_fs *fs, int entry_idx);

bool entry_is_first_extent(struct cpm_fs *fs, uint32_t extent);
uint32_t get_last_extent(struct cpm_fs *fs, cpm_entry *entry);

/* Logical extents */
uint32_t extent_nb(cpm_entry *entry);
void set_extent_nb(cpm_entry *entry, uint32_t number);

/* ---- Blocks --------------------------------------------------------- */

/* True if the block index is the last used block of given entry */
bool is_last_block(struct cpm_fs *fs, cpm_entry *entry, uint16_t idx);

/* Number of used blocks in given entry (at most 16) */
uint8_t get_used_blocks(struct cpm_fs *fs, cpm_entry *entry);

/* Max number of blocks in an entry */
uint8_t max_blocks_per_entry(struct cpm_fs *fs);

void entry_set_block(struct cpm_fs *fs,
		     cpm_entry *entry,
		     uint8_t idx,
		     uint16_t block);

uint16_t find_free_block(struct cpm_fs *fs);
