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

/* If the disk has less than 256 available blocks (including directory area),
 * block_ptr is interpreted as 16 integers. Otherwise it's 8 2-byte values. */
enum cpm_fs_block_addressing {
	CPM_BLOCK_ADDR_8, /* 8 bit block pointers */
	CPM_BLOCK_ADDR_16, /* 16 bit block pointers */
};

/* File Control Block (FCB) = Directory Entry = Extent */
typedef struct {
	uint8_t status;
	uint8_t file[8];
	uint8_t extension[3];
	uint8_t extent_l;
	uint8_t bc; /* Unused in 2.2 */
	uint8_t extent_h;
	uint8_t rc; /* Number of 128 byte records of last extent */
	union {
		uint8_t block_ptr[16];
		uint8_t block_ptr_w[8];
	};
} __attribute__((packed, aligned(1))) cpm_entry;

struct cpm_superblock {
	uint32_t count;
	cpm_entry *entries;
};

struct cpm_fs_file_handle {
	uint8_t header[13]; /* Status + Filename + Extension */
	uint32_t entry;
	uint32_t block; /* Current block number */
	uint32_t offset; /* Offset in current block */
};

struct cpm_fs_dir {
	struct cpm_fs *fs;
	struct cpm_fs_file file;
	int32_t current_file_ino;
};

struct cpm_fs {
	struct cpm_fs_attr attr;
	struct cpm_superblock superblock;

	uint32_t disk_size;
	enum cpm_fs_block_addressing block_addressing;

	struct {
		uint8_t *buf;
		uint32_t c;
		uint32_t h;
		uint32_t s;
	} cache;

	get_sector_cb get_sector;
	void *userdata;
};

enum cpm_fs_status check_superblock(struct cpm_fs *fs);
bool cpm_entry_is_valid(const cpm_entry *entry);

uint32_t extent_nb(cpm_entry *entry);

/* Returns first entry for pathname. Extension doesn't include status flags */
int find_file(struct cpm_fs *fs, const char *pathname, int user);

/* Return next extent for file. 0 if none found */
uint32_t get_next_extent(struct cpm_fs *fs, uint32_t extent);

/* For given block and offset, return matching sector */
void block_to_chs(struct cpm_fs *fs,
		  uint32_t block,
		  uint32_t block_offset,
		  uint32_t *c,
		  uint32_t *h,
		  uint32_t *s);

/* Return number of the last extent associated with given entry */
uint32_t get_last_extent(struct cpm_fs *fs, cpm_entry *entry);
bool is_last_block(struct cpm_fs *fs, cpm_entry *entry, uint16_t block);
bool is_first_extent(struct cpm_fs *fs, uint32_t extent);
uint8_t get_used_blocks(struct cpm_fs *fs, cpm_entry *entry);
uint32_t get_filesize(struct cpm_fs *fs, cpm_entry *entry);
