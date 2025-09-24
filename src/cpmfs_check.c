/* Copyright (c) 2025 Arthur DAUZAT
 * SPDX-License-Identifier: BSD-3-Clause */

#include <stdlib.h>
#include <string.h>

#include "cpmfs_internal.h"
#include "libcpmfs.h"

/* Check for blocks out of bounds */
static int check_block_validity(struct cpm_fs *fs, cpm_entry *file)
{
	uint32_t max_blocks, dir_blocks;

	/* Maximum block number */
	max_blocks = fs->disk_size / fs->attr.block_size;
	/* TODO: this should ignore boot tracks */

	/* Number of blocks reserved for directory table */
	dir_blocks = (fs->attr.max_dir_entries * 32) / fs->attr.block_size;
	if ((fs->attr.max_dir_entries * 32) % fs->attr.block_size)
		dir_blocks += 1;

	if (fs->block_addressing == CPM_BLOCK_ADDR_8) {
		for (int i = 0; i < 16; ++i) {
			if (file->block_ptr[i] > max_blocks)
				return CPM_ERR_BLOCK_OVERFLOW;
			else if (file->block_ptr[i] &&
				 file->block_ptr[i] <= dir_blocks - 1) {
				return CPM_ERR_FILE_DIR_OVERLAP;
			}
		}
	} else {
		uint16_t *blocks = (uint16_t *)file->block_ptr;
		for (int i = 0; i < 8; ++i) {
			if (blocks[i] > max_blocks)
				return CPM_ERR_BLOCK_OVERFLOW;
			else if (blocks[i] && blocks[i] <= dir_blocks - 1)
				return CPM_ERR_FILE_DIR_OVERLAP;
		}
	}
	return 0;
}

static int qsort_comparator(const void *a, const void *b)
{
	uint8_t f = *((uint8_t *)a);
	uint8_t s = *((uint8_t *)b);
	return (f > s ? 1 : (f < s) ? -1 : 0);
}

static int qsort_comparator_w(const void *a, const void *b)
{
	uint16_t f = *((uint16_t *)a);
	uint16_t s = *((uint16_t *)b);
	return (f > s ? 1 : (f < s) ? -1 : 0);
}

static int is_allowed_char(char c)
{
	c = c & 0x7f;
	/* Unallowed characters */
	if (c == '<' || c == '>' || c == '.' || c == ',' || c == ';' ||
	    c == ':' || c == '=' || c == '?' || c == '*' || c == '[' ||
	    c == ']')
		return 0;
	else if (c < 0x20)
		return 0;
	return 1;
}

bool cpm_entry_is_valid(const cpm_entry *entry)
{
	int i;

	if (entry->status == 0xE5)
		return false;

	/* Invalid filename / extension */
	for (i = 0; i < 8; ++i)
		if (!is_allowed_char(entry->file[i]))
			return false;
	for (i = 0; i < 3; ++i)
		if (!is_allowed_char(entry->extension[i]))
			return false;

	return true;
}

/* Check for multiple files using the same block */
static int check_extent_overlap_wide(struct cpm_fs *fs)
{
	uint16_t *extent_list;
	uint32_t i;
	int ret = 0;

	extent_list = calloc(fs->attr.max_dir_entries * 8, 2);
	if (!extent_list)
		return CPM_ERR_NOMEM;

	/* Fill table with valid file extents */
	for (i = 0; i < fs->attr.max_dir_entries; ++i)
		if (cpm_entry_is_valid(&fs->superblock.entries[i]))
			memcpy(((uint8_t *)extent_list) + i * 16,
			       fs->superblock.entries[i].block_ptr,
			       16);

	qsort(extent_list, fs->attr.max_dir_entries * 8, 2, qsort_comparator_w);
	for (i = 0; i < fs->attr.max_dir_entries * 8 - 1; ++i)
		if (extent_list[i] && extent_list[i] == extent_list[i + 1])
			ret = CPM_ERR_FILE_OVERLAP;

	free(extent_list);
	return ret;
}

static int check_extent_overlap(struct cpm_fs *fs)
{
	uint8_t *extent_list;
	uint32_t i;
	int ret = 0;

	extent_list = calloc(16 * fs->superblock.count + 1, 1);
	if (!extent_list)
		return CPM_ERR_NOMEM;

	/* Copy and sort all the extent numbers to check for overlaps */
	for (i = 0; i < fs->superblock.count; ++i)
		if (cpm_entry_is_valid(&fs->superblock.entries[i]))
			memcpy(extent_list + i * 16,
			       fs->superblock.entries[i].block_ptr,
			       16);

	qsort(extent_list, fs->superblock.count * 16, 1, qsort_comparator);
	for (i = 0; i < fs->superblock.count * 16; ++i)
		if (extent_list[i] && extent_list[i] == extent_list[i + 1])
			ret = CPM_ERR_FILE_OVERLAP;

	free(extent_list);
	return ret;
}

enum cpm_fs_status check_superblock(struct cpm_fs *fs)
{
	uint32_t i = 0;
	int ret = 0;

	for (i = 0; i < fs->attr.max_dir_entries; ++i) {
		if (!cpm_entry_is_valid(&(fs->superblock.entries[i])))
			continue;

		ret = check_block_validity(fs, &(fs->superblock.entries[i]));
		if (ret)
			return ret;
	}

	if (fs->block_addressing == CPM_BLOCK_ADDR_8)
		ret = check_extent_overlap(fs);
	else
		ret = check_extent_overlap_wide(fs);

	return ret;
}
