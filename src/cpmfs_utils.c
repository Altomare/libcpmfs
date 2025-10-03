/* Copyright (c) 2025 Arthur DAUZAT
 * SPDX-License-Identifier: BSD-3-Clause */

#include <string.h>

#include "cpmfs_internal.h"

uint32_t extent_nb(cpm_entry *entry)
{
	return ((entry->extent_h & 0x3f) << 5) | (entry->extent_l & 0x1f);
}

uint32_t get_disk_size(struct cpm_fs *fs)
{
	uint32_t cylinders;

	cylinders = fs->attr.cylinders * fs->attr.heads;
	cylinders -= fs->attr.skip_first_cylinders;
	cylinders -= fs->attr.boot_cylinders * fs->attr.heads;

	return cylinders * fs->attr.sector_size * fs->attr.sector_count;
}

int find_file(struct cpm_fs *fs, const char *pathname, int user)
{
	cpm_entry *entry;
	char *file = NULL;
	char *tmp = NULL;
	char ext[3];
	size_t file_len, ext_len;
	int file_extent = -1;

	file = (char *)pathname;
	if (*file == '/')
		++file;

	tmp = strchr(file, '.');
	if (tmp) {
		file_len = (size_t)(tmp - file);
		tmp += 1;
		ext_len = 3;
		for (int i = 0; i < 3; ++i)
			ext[i] = tmp[i] & 0x7f;
		if (strchr(tmp, ' '))
			ext_len = (size_t)(strchr(tmp, ' ') - tmp);
	} else {
		file_len = strlen(file);
		ext_len = 0;
	}

	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		entry = &fs->superblock.entries[i];
		if (entry->status != user)
			continue;
		if (memcmp(file, entry->file, file_len) != 0 ||
		    memcmp(ext, entry->extension, ext_len) != 0)
			continue;

		/* Make sure to return the first extent */
		if (file_extent == -1 ||
		    extent_nb(entry) <
			    extent_nb(&fs->superblock.entries[file_extent]))
			file_extent = i;
	}

	return file_extent;
}

uint32_t get_next_extent(struct cpm_fs *fs, uint32_t extent)
{
	cpm_entry *entry = &fs->superblock.entries[extent];
	cpm_entry *tmp;
	uint32_t cur_extent = extent_nb(entry);
	uint32_t tmp_extent;
	uint32_t res = extent;

	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		if (i == extent)
			continue;
		tmp = &fs->superblock.entries[i];
		/* Compare status, filename and extension */
		if (memcmp(tmp, entry, 12) != 0)
			continue;
		tmp_extent = extent_nb(tmp);
		if (tmp_extent <= cur_extent)
			continue;
		if (res == extent ||
		    extent_nb(&fs->superblock.entries[res]) > tmp_extent)
			res = i;
	}
	if (res == extent)
		return 0;
	return res;
}

void block_to_chs(struct cpm_fs *fs,
		  uint32_t block,
		  uint32_t block_offset,
		  uint32_t *c,
		  uint32_t *h,
		  uint32_t *s)
{
	uint32_t offset;
	uint32_t sector;
	uint32_t temp;

	offset = block * fs->attr.block_size + block_offset;

	/* Ignore reserved cylindres */
	offset += fs->attr.skip_first_cylinders * fs->attr.sector_count *
		  fs->attr.sector_size;
	offset += fs->attr.boot_cylinders * fs->attr.sector_count *
		  fs->attr.sector_size * fs->attr.heads;

	sector = offset / fs->attr.sector_size;
	temp = sector % fs->attr.sector_count;

	*c = (sector / fs->attr.sector_count) % fs->attr.cylinders;
	*h = (sector / fs->attr.sector_count) / fs->attr.cylinders;
	*s = (temp % fs->attr.sector_count) + 1;
}

uint32_t get_last_extent(struct cpm_fs *fs, cpm_entry *entry)
{
	uint32_t extent = 0;
	cpm_entry *tmp;

	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		tmp = &fs->superblock.entries[i];
		if (memcmp(tmp, entry, 12) != 0)
			continue;
		if (extent_nb(tmp) > extent)
			extent = extent_nb(tmp);
	}
	return extent;
}

bool is_last_block(struct cpm_fs *fs, cpm_entry *entry, uint16_t block)
{
	if (fs->block_addressing == CPM_BLOCK_ADDR_8) {
		if (block == 15 || entry->block_ptr[block + 1] == 0)
			return true;
	} else {
		if (block == 7 || entry->block_ptr_w[block + 1] == 0)
			return true;
	}
	return false;
}

bool is_first_extent(struct cpm_fs *fs, uint32_t extent)
{
	cpm_entry *entry = &fs->superblock.entries[extent];
	cpm_entry *tmp;

	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		if (i == extent)
			continue;
		tmp = &fs->superblock.entries[i];
		/* Compare status, filename and extension */
		if (memcmp(tmp, entry, 12) != 0)
			continue;
		if (tmp->extent_l < entry->extent_l)
			return false;
	}
	return true;
}

uint8_t get_used_blocks(struct cpm_fs *fs, cpm_entry *entry)
{
	uint8_t count = 0;
	if (fs->block_addressing == CPM_BLOCK_ADDR_8) {
		for (int i = 0; i < 16; ++i) {
			if (entry->block_ptr[i])
				count++;
		}
	} else {
		for (int i = 0; i < 8; ++i) {
			if (entry->block_ptr_w[i])
				count++;
		}
	}
	return count;
}

uint32_t get_filesize(struct cpm_fs *fs, cpm_entry *entry)
{
	uint32_t size = 0;
	cpm_entry *tmp;
	uint32_t last_extent = get_last_extent(fs, entry);

	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		tmp = &fs->superblock.entries[i];
		/* Compare status, filename and extension */
		if (memcmp(tmp, entry, 12) != 0)
			continue;

		if (extent_nb(tmp) == last_extent)
			size += 128 * tmp->rc;
		else
			size += get_used_blocks(fs, tmp) * fs->attr.block_size;
	}
	return size;
}
