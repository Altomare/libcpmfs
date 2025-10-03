/* Copyright (c) 2025 Arthur DAUZAT
 * SPDX-License-Identifier: BSD-3-Clause */

#include <string.h>

#include "cpmfs_internal.h"

struct cpm_fs_dir *cpm_fs_opendir(struct cpm_fs *fs)
{
	struct cpm_fs_dir *dir;

	dir = calloc(sizeof(struct cpm_fs_dir), 1);
	if (!dir)
		return NULL;

	dir->fs = fs;
	dir->current_file_ino = -1;
	return dir;
}

struct cpm_fs_file_handle *
cpm_fs_open(struct cpm_fs *fs, const char *pathname, int user)
{
	struct cpm_fs_file_handle *file;
	int entry;

	if (!pathname || user < 0 || user > 15)
		return NULL;

	entry = find_file(fs, pathname, user);
	if (entry == -1)
		return NULL;

	file = calloc(sizeof(struct cpm_fs_file_handle), 1);
	if (!file)
		return NULL;

	memcpy(file->header, &fs->superblock.entries[entry], 13);
	file->entry = entry;
	file->block = 0;
	file->offset = 0;

	return file;
}

static int read_sector(struct cpm_fs *fs, uint32_t c, uint32_t h, uint32_t s)
{
	int ret;

	if (fs->cache.c == c && fs->cache.h == h && fs->cache.s == s)
		return 0;

	ret = fs->get_sector(fs->userdata, c, h, s, fs->cache.buf);
	fs->cache.c = (ret == 0 ? c : 0);
	fs->cache.h = (ret == 0 ? h : 0);
	fs->cache.s = (ret == 0 ? s : 0);
	return ret;
}

ssize_t cpm_fs_read(struct cpm_fs *fs,
		    struct cpm_fs_file_handle *file_handle,
		    void *buf,
		    size_t count)
{
	cpm_entry *entry;
	uint16_t block;
	uint32_t block_size;
	uint32_t last_extent;
	uint32_t c;
	uint32_t h;
	uint32_t s;
	int ret = 0;

	if (!fs || !file_handle || !buf || !count)
		return -1;

	entry = &fs->superblock.entries[file_handle->entry];
	last_extent = get_last_extent(fs, entry);
	while (count) {
		if (fs->block_addressing == CPM_BLOCK_ADDR_8)
			block = entry->block_ptr[file_handle->block];
		else
			block = entry->block_ptr_w[file_handle->block];

		if (!block) /* EOF */
			break;

		/* Last block size is determined by RC */
		block_size = fs->attr.block_size;
		if (extent_nb(entry) == last_extent &&
		    is_last_block(fs, entry, file_handle->block))
			block_size = 128 * entry->rc -
				     fs->attr.block_size * file_handle->block;

		block_to_chs(fs, block, file_handle->offset, &c, &h, &s);
		ret = read_sector(fs, c, h, s);
		if (ret != 0)
			return -1;

		uint32_t block_offset =
			file_handle->offset % fs->attr.sector_size;

		/* Compute size to read */
		uint32_t size_to_read = fs->attr.sector_size;
		if (block_size - block_offset < fs->attr.sector_size)
			size_to_read = block_size - block_offset;
		if (count < size_to_read)
			size_to_read = count;

		memcpy(buf, fs->cache.buf + block_offset, size_to_read);
		count -= size_to_read;
		file_handle->offset += size_to_read;
		buf += size_to_read;

		/* Keep reading the same block */
		if (file_handle->offset < block_size)
			continue;

		/* Next block */
		file_handle->block += 1;
		file_handle->offset = 0;
		if ((fs->block_addressing == CPM_BLOCK_ADDR_8 &&
		     file_handle->block >= 16) ||
		    (fs->block_addressing == CPM_BLOCK_ADDR_16 &&
		     file_handle->block >= 8)) {
			file_handle->entry =
				get_next_extent(fs, file_handle->entry);
			file_handle->block = 0;
			if (!file_handle->entry) /* EOF */
				break;
			entry = &fs->superblock.entries[file_handle->entry];
			last_extent = get_last_extent(fs, entry);
		}
	}

	return ret;
}

void cpm_fs_close(struct cpm_fs *fs, struct cpm_fs_file_handle *file_handle)
{
	fs = fs;

	if (file_handle)
		free(file_handle);
}

struct cpm_fs_file *cpm_fs_readdir(struct cpm_fs *fs, struct cpm_fs_dir *dirp)
{
	cpm_entry *entry;
	uint8_t *filename_out;

	while ((uint32_t)++dirp->current_file_ino < fs->superblock.count) {
		if (cpm_entry_is_valid(
			    &fs->superblock.entries[dirp->current_file_ino]) &&
		    is_first_extent(fs, dirp->current_file_ino))
			break;
	}
	if ((uint32_t)dirp->current_file_ino >= fs->superblock.count)
		return NULL;

	entry = &fs->superblock.entries[dirp->current_file_ino];

	/* Copy file name without status flags */
	filename_out = (uint8_t *)dirp->file.d_name;
	memset(&dirp->file, 0, sizeof(struct cpm_fs_file));
	int i = -1;
	while (++i < 8 && (entry->file[i] & 0x7f) != 0x20)
		*(filename_out++) = entry->file[i] & 0x7f;
	if ((entry->extension[0] & 0x7f) != 0x20) {
		*(filename_out++) = '.';
		i = -1;
		while (++i < 3 && (entry->extension[i] & 0x7f) != 0x20)
			*(filename_out++) = entry->extension[i] & 0x7f;
	}

	/* File attributes, CP/M 2.2 attributes only */
	if (F_IS_READONLY(entry))
		dirp->file.d_flags |= CPM_FS_FLAG_READONLY;
	if (F_IS_SYSTEMFILE(entry))
		dirp->file.d_flags |= CPM_FS_FLAG_SYSTEM;
	if (F_IS_ARCHIVED(entry))
		dirp->file.d_flags |= CPM_FS_FLAG_ARCHIVED;

	dirp->file.d_user = entry->status & 0x0F;
	dirp->file.d_size = get_filesize(fs, entry);

	return &dirp->file;
}

void cpm_fs_closedir(struct cpm_fs_dir *dir)
{
	if (dir)
		free(dir);
}

/* Store superblock and parse directory entries */
static int read_superblock(struct cpm_fs *fs)
{
	int i = 0, j = 0;
	uint32_t c, h, s;
	struct cpm_superblock *sb = &fs->superblock;
	int ret;

	/* Locate superblock */
	c = fs->attr.skip_first_cylinders | fs->attr.boot_cylinders;
	h = 0;
	s = 1;

	sb->count = fs->attr.max_dir_entries;
	sb->entries = malloc(sizeof(cpm_entry) * sb->count);
	if (!sb->entries)
		return CPM_ERR_NOMEM;

	while ((uint32_t)i < sb->count) {
		j = 0;
		ret = fs->get_sector(fs->userdata, c, h, s, fs->cache.buf);
		if (ret)
			return ret;

		while (j * sizeof(cpm_entry) < fs->attr.sector_size) {
			memcpy(&sb->entries[i + j],
			       fs->cache.buf + j * sizeof(cpm_entry),
			       sizeof(cpm_entry));
			j++;
		}
		i += j;

		/* Next sector */
		if (++s > fs->attr.sector_count) {
			s = 0;
			c++;
		}
	}

	/* Available disk size for extents */
	fs->disk_size = get_disk_size(fs);
	if (fs->disk_size <= 256 * fs->attr.block_size)
		fs->block_addressing = CPM_BLOCK_ADDR_8;
	else
		fs->block_addressing = CPM_BLOCK_ADDR_16;

	return 0;
}

enum cpm_fs_status cpm_fs_new(struct cpm_fs_attr *attributes,
			      get_sector_cb sector_cb,
			      void *userdata,
			      struct cpm_fs **out)
{
	struct cpm_fs *fs;
	int err = 0;

	if (!sector_cb || !attributes)
		return CPM_ERR_INVALID_ARG;

	/* Mutually exclusive, only one or none must be set */
	if (attributes->skip_first_cylinders && attributes->boot_cylinders)
		return CPM_ERR_INVALID_ARG;

	fs = calloc(sizeof(struct cpm_fs), 1);
	if (!fs)
		return CPM_ERR_NOMEM;

	fs->attr = *attributes;
	fs->get_sector = sector_cb;
	fs->userdata = userdata;
	fs->cache.buf = calloc(fs->attr.sector_size, 1);
	if (!fs->cache.buf) {
		err = CPM_ERR_NOMEM;
		goto error;
	}

	if ((err = read_superblock(fs)))
		goto error;

	if ((err = check_superblock(fs)))
		goto error;

	*out = fs;

	return CPM_SUCCESS;
error:
	cpm_fs_destroy(fs);
	*out = NULL;
	return err;
}

void cpm_fs_destroy(struct cpm_fs *fs)
{
	if (!fs)
		return;
	if (fs->superblock.entries) {
		free(fs->superblock.entries);
		fs->superblock.entries = NULL;
	}
	if (fs->cache.buf) {
		free(fs->cache.buf);
		fs->cache.buf = NULL;
	}
	free(fs);
}
