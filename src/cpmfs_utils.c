/* Copyright (c) 2025 Arthur DAUZAT
 * SPDX-License-Identifier: BSD-3-Clause */

#include <string.h>

#include "cpmfs_internal.h"

uint32_t extent_nb(cpm_entry *entry)
{
	return ((entry->extent_h & 0x3f) << 5) | (entry->extent_l & 0x1f);
}

void set_extent_nb(cpm_entry *entry, uint32_t number)
{
	entry->extent_l = (uint8_t)(number & 0x1f);
	entry->extent_h = (uint8_t)((number >> 5) & 0x3f);
}

bool is_valid_user(int user)
{
	return (user >= 0 && user <= 15);
}

bool is_allowed_char(char c)
{
	c = c & 0x7f;
	/* Unallowed characters, as defined in CP/M 2.0 user manual */
	if (c == '<' || c == '>' || c == '.' || c == ',' || c == ';' ||
	    c == ':' || c == '=' || c == '?' || c == '*' || c == '[' ||
	    c == ']')
		return 0;
	else if (c < 0x20)
		return 0;
	return 1;
}

int av_build(struct cpm_fs *fs)
{
	uint32_t dir_blocks;

	fs->av = (uint8_t *)calloc(
		(fs->disk_size / fs->attr.block_size) / 8 + 1, 1);
	if (!fs->av)
		return CPM_ERR_NOMEM;

	/* Mark directory blocks as used */
	dir_blocks = (fs->attr.max_dir_entries * sizeof(cpm_entry) +
		      fs->attr.block_size - 1) /
		     fs->attr.block_size;

	for (uint32_t i = 0; i < dir_blocks; ++i)
		av_set(fs, i);

	/* Mark blocks referenced by valid directory entries */
	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		cpm_entry *entry = &fs->superblock.entries[i];
		if (!cpm_entry_is_valid(entry))
			continue;

		if (fs->block_addressing == CPM_BLOCK_ADDR_8) {
			for (int j = 0; j < 16; ++j)
				if (entry->block_ptr[j] > 0)
					av_set(fs, entry->block_ptr[j]);
		} else {
			for (int j = 0; j < 8; ++j)
				if (entry->block_ptr_w[j])
					av_set(fs, entry->block_ptr_w[j]);
		}
	}

	return CPM_SUCCESS;
}

void av_set(struct cpm_fs *fs, int block_index)
{
	fs->av[block_index / 8] |= (1u << (block_index % 8));
}

void av_unset(struct cpm_fs *fs, int block_index)
{
	fs->av[block_index / 8] &= (~(1u << (block_index % 8)));
}

int av_get(struct cpm_fs *fs, int block_index)
{
	return fs->av[block_index / 8] & (1u << (block_index % 8));
}

/* Available disk size for files and superblock, in bytes */
uint32_t get_disk_size(struct cpm_fs *fs)
{
	uint32_t cylinders;

	cylinders = fs->attr.cylinders * fs->attr.heads;
	cylinders -= fs->attr.boot_cylinders;

	return cylinders * fs->attr.sector_size * fs->attr.sector_count;
}

/* Return 0 if identical */
static int compare_ext(cpm_entry *entry, const char *ext, size_t ext_len)
{
	for (size_t i = 0; i < ext_len; ++i)
		if ((entry->extension[i] & 0x7F) != ext[i])
			return -1;
	return 0;
}

/* Returns first entry for pathname. Extension doesn't include status flags */
int32_t find_file(struct cpm_fs *fs, const char *pathname, int user)
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
		for (int i = 0; i < 3; ++i)
			ext[i] = tmp[i] & 0x7f;
		ext_len = strnlen(ext, 3);
		if (strchr(ext, ' '))
			ext_len = (size_t)(strchr(ext, ' ') - ext);
	} else {
		file_len = strlen(file);
		ext_len = 0;
	}
	if (file_len == 0)
		return 1;

	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		entry = &fs->superblock.entries[i];
		if (entry->status != user)
			continue;
		if (memcmp(file, entry->file, file_len) != 0 ||
		    compare_ext(entry, ext, ext_len) != 0)
			continue;

		/* Make sure to return the first extent */
		if (file_extent == -1 ||
		    extent_nb(entry) <
			    extent_nb(&fs->superblock.entries[file_extent]))
			file_extent = i;
	}

	return file_extent;
}

/* If the filename is valid, return zero and store pointers & lengths.
 * Otherwise, return 1.
 * Spaces are not trimmed */
int parse_filename(const char *pathname,
		   char **out_file,
		   size_t *out_filelen,
		   char **out_ext,
		   size_t *out_extlen)
{
	char *file, *ext;

	file = (char *)pathname;
	if (*file == '/')
		++file;

	ext = strchr(file, '.');
	if (ext) {
		*out_filelen = (size_t)(ext - file);
		ext += 1;
		*out_extlen = strlen(ext);
		if (strchr(ext, ' '))
			*out_extlen = (size_t)(strchr(ext, ' ') - ext);
	} else {
		*out_filelen = strlen(file);
		*out_extlen = 0;
	}

	for (size_t i = 0; i < *out_filelen; ++i)
		if (!is_allowed_char(file[i]))
			return CPM_ERR_FILENAME_INVALID;
	for (size_t i = 0; i < *out_extlen; ++i)
		if (!is_allowed_char(ext[i]))
			return CPM_ERR_FILENAME_INVALID;

	*out_file = file;
	if (*out_extlen)
		*out_ext = ext;
	return 0;
}

static void init_entry(cpm_entry *entry)
{
	/* Default values for allocated entry */
	memset(entry, 0, sizeof(*entry));
	entry->status = 0xE5;
	memset(entry->file, 0x20, 8);
	memset(entry->extension, 0x20, 3);
}

void wipe_extent(struct cpm_fs *fs, int entry_idx)
{
	cpm_entry *entry = &fs->superblock.entries[entry_idx];
	uint32_t dir_blocks = (fs->attr.max_dir_entries * sizeof(cpm_entry) +
			       fs->attr.block_size - 1) /
			      fs->attr.block_size;

	if (fs->block_addressing == CPM_BLOCK_ADDR_8) {
		for (int i = 0; i < 16; ++i)
			if (entry->block_ptr[i] > dir_blocks)
				av_unset(fs, entry->block_ptr[i]);
	} else {
		for (int i = 0; i < 8; ++i)
			if (entry->block_ptr_w[i] > dir_blocks)
				av_unset(fs, entry->block_ptr_w[i]);
	}

	entry->status = 0xE5;
	memset(entry->block_ptr, 0, 16);
}

static int find_free_entry_idx(struct cpm_fs *fs)
{
	for (uint32_t i = 0; i < fs->superblock.count; ++i)
		if (fs->superblock.entries[i].status == 0xE5)
			return (int)i;
	return 1;
}

int create_file(struct cpm_fs *fs, const char *pathname, int user)
{
	size_t file_len, ext_len;
	cpm_entry *entry;
	int idx;
	int ret;
	char *file = NULL;
	char *ext = NULL;

	if (user < 0 || user >= 15)
		return CPM_ERR_INVALID_USER;

	/* Check if filename is valid */
	ret = parse_filename(pathname, &file, &file_len, &ext, &ext_len);
	if (ret != 0)
		return ret;

	if (find_file(fs, pathname, user) != -1)
		return CPM_ERR_FILE_ALREADY_EXISTS;

	idx = find_free_entry_idx(fs);
	if (idx < 0)
		return CPM_ERR_DISK_FULL;

	/* Flags are not set yet */
	entry = &fs->superblock.entries[idx];
	init_entry(entry);
	entry->status = user;
	memcpy(entry->file, file, file_len);
	if (ext)
		memcpy(entry->extension, ext, ext_len);

	/* Superblock is not written here, this should be done by the caller */
	return 0;
}

int alloc_new_extent(struct cpm_fs *fs, cpm_entry *src_entry)
{
	uint32_t number;
	cpm_entry *new_entry;
	int extent;

	extent = find_free_entry_idx(fs);
	if (extent < 0)
		return CPM_ERR_DISK_FULL;

	new_entry = &fs->superblock.entries[extent];
	number = get_last_extent(fs, src_entry) + 1;

	/* Copy last extent and update flags */
	memcpy(new_entry, src_entry, sizeof(cpm_entry));
	set_extent_nb(new_entry, number);
	new_entry->bc = 0;
	new_entry->rc = 0;
	memset(new_entry->block_ptr, 0, 16);

	return extent;
}

/* Return next extent for file. 0 if none found */
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

/* For given block and offset, return matching sector */
void block_to_chs(struct cpm_fs *fs,
		  uint32_t block,
		  uint32_t block_offset,
		  uint32_t *c,
		  uint32_t *h,
		  uint32_t *s)
{
	uint32_t offset;
	uint32_t sector;
	uint32_t side_size;

	offset = block * fs->attr.block_size + block_offset;
	sector = offset / fs->attr.sector_size;

	*c = (sector / fs->attr.sector_count) + fs->attr.boot_cylinders;
	*s = (sector % fs->attr.sector_count) + 1;

	if (fs->attr.hcs_fill) {
		*h = (sector / fs->attr.sector_count) / fs->attr.cylinders;
		*c = *c % fs->attr.cylinders;
	} else {
		*h = *c % fs->attr.heads;
		*c = *c / fs->attr.heads;
	}

	/* Apply skew factor */
	if (fs->attr.skew_table != NULL)
		*s = fs->attr.skew_table[*s - 1];
}

/* Return number of the last physical extent associated with given entry */
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

bool entry_is_first_extent(struct cpm_fs *fs, uint32_t extent)
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
		if (extent_nb(tmp) < extent_nb(entry))
			return false;
	}
	return true;
}

uint32_t get_filesize(struct cpm_fs *fs, cpm_entry *entry)
{
	uint32_t last_extent = get_last_extent(fs, entry);
	uint32_t size = 0;
	cpm_entry *tmp;
	uint8_t used_blocks;
	/* How many blocks per logical extent (16k) */
	uint8_t block_per_extent = 0x4000 / fs->attr.block_size;

	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		tmp = &fs->superblock.entries[i];
		/* Compare status, filename and extension */
		if (memcmp(tmp, entry, 12) != 0)
			continue;

		used_blocks = get_used_blocks(fs, tmp);
		/* RC specifies the size of the last extent, a disk entry might
		 * contain multiple logical extents */
		if (extent_nb(tmp) == last_extent) {
			/* Size of full logical extents in entry */
			if (used_blocks > 0)
				size += 0x4000 *
					((used_blocks - 1) / block_per_extent);
			size += 128 * tmp->rc;
		} else {
			size += used_blocks * fs->attr.block_size;
		}
	}
	return size;
}

/* Block utils */

bool is_last_block(struct cpm_fs *fs, cpm_entry *entry, uint16_t idx)
{
	if (fs->block_addressing == CPM_BLOCK_ADDR_8) {
		if (idx == 15 || entry->block_ptr[idx + 1] == 0)
			return true;
	} else {
		if (idx == 7 || entry->block_ptr_w[idx + 1] == 0)
			return true;
	}
	return false;
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

uint8_t max_blocks_per_entry(struct cpm_fs *fs)
{
	return (fs->block_addressing == CPM_BLOCK_ADDR_8) ? 16 : 8;
}

void entry_set_block(struct cpm_fs *fs,
		     cpm_entry *entry,
		     uint8_t idx,
		     uint16_t block)
{
	if (fs->block_addressing == CPM_BLOCK_ADDR_8)
		entry->block_ptr[idx] = (uint8_t)block;
	else
		entry->block_ptr_w[idx] = block;
}

/* Return the number of the first free (unallocated) block, or 0 on failure. */
/* Block 0 is always used by the superblock so we can use it for error */
uint16_t find_free_block(struct cpm_fs *fs)
{
	uint16_t total_blocks = fs->disk_size / fs->attr.block_size;

	for (uint16_t i = 1; i < total_blocks; ++i)
		if (av_get(fs, i) == 0)
			return i;

	return 0; /* Disk full */
}
