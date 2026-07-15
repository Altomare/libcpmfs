/* Copyright (c) 2025 Arthur DAUZAT
 * SPDX-License-Identifier: BSD-3-Clause */

#include <string.h>

#include "cpmfs_internal.h"

static int write_superblock(struct cpm_fs *fs)
{
	uint32_t i, j;
	uint32_t c, h, s;
	/* Number of entries per sector */
	uint32_t entries_c = fs->attr.sector_size / sizeof(cpm_entry);
	int ret;

	block_to_chs(fs, 0, 0, &c, &h, &s);
	i = 0;
	while (i < fs->superblock.count) {
		memset(fs->cache, 0xE5, fs->attr.sector_size);
		for (j = 0; j < entries_c && i + j < fs->superblock.count; ++j)
			memcpy(fs->cache + j * sizeof(cpm_entry),
			       &fs->superblock.entries[i + j],
			       sizeof(cpm_entry));

		ret = fs->write_sector(fs->userdata, c, h, s, fs->cache);
		if (ret)
			return CPM_ERR_SECTOR_WRITE;

		i += j;
		block_to_chs(fs, 0, (i * sizeof(cpm_entry)), &c, &h, &s);
	}

	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_opendir(struct cpm_fs *fs,
				  struct cpm_fs_dir **out_dir)
{
	if (!fs || !out_dir)
		return CPM_ERR_INVALID_ARG;

	*out_dir = (struct cpm_fs_dir *)calloc(sizeof(struct cpm_fs_dir), 1);
	if (*out_dir == NULL)
		return CPM_ERR_NOMEM;

	(*out_dir)->fs = fs;
	(*out_dir)->current_file_ino = -1;
	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_open(struct cpm_fs *fs,
			       const char *pathname,
			       enum cpm_fs_mode mode,
			       int user,
			       struct cpm_fs_file_handle **out_file)
{
	int entry;
	int ret;

	if (!fs || !pathname || mode < CPM_MODE_RDONLY ||
	    mode > CPM_MODE_RDWR || !out_file)
		return CPM_ERR_INVALID_ARG;

	if (!is_valid_user(user))
		return CPM_ERR_INVALID_USER;

	entry = find_file(fs, pathname, user);
	if (entry == -1) {
		if (mode == CPM_MODE_RDONLY)
			/* File not found, won't create it because read-only */
			return CPM_ERR_FILE_NOT_FOUND;
		else {
			ret = create_file(fs, pathname, user);
			if (ret != CPM_SUCCESS)
				return ret;
			entry = find_file(fs, pathname, user);
			if (entry == -1)
				/* Should never happen */
				return CPM_ERR_FILE_NOT_FOUND;
		}
	}

	*out_file = (struct cpm_fs_file_handle *)calloc(
		sizeof(struct cpm_fs_file_handle), 1);
	if (*out_file == NULL)
		return CPM_ERR_NOMEM;

	(*out_file)->entry = entry;
	(*out_file)->block = 0;
	(*out_file)->offset = 0;
	(*out_file)->mode = mode;

	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_read(struct cpm_fs *fs,
			       struct cpm_fs_file_handle *file_handle,
			       uint8_t *buf,
			       size_t count,
			       size_t *out_read)
{
	cpm_entry *entry;
	uint16_t block;
	uint32_t block_size;
	uint32_t last_extent;
	uint32_t c, h, s;
	int ret = 0;

	if (!fs || !file_handle || !buf || count == 0 || !out_read)
		return CPM_ERR_INVALID_ARG;

	*out_read = 0;
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
		    is_last_block(fs, entry, file_handle->block) &&
		    (entry->rc * 128) % fs->attr.block_size != 0)
			block_size = (entry->rc * 128) % fs->attr.block_size;

		block_to_chs(fs, block, file_handle->offset, &c, &h, &s);
		ret = fs->read_sector(fs->userdata, c, h, s, fs->cache);
		if (ret != 0)
			return CPM_ERR_SECTOR_READ;

		uint32_t block_offset =
			file_handle->offset % fs->attr.sector_size;

		/* Compute size to read */
		uint32_t size_to_read = fs->attr.sector_size;
		if (block_size - block_offset < fs->attr.sector_size)
			size_to_read = block_size - block_offset;
		if (count < size_to_read)
			size_to_read = count;

		memcpy(buf, fs->cache + block_offset, size_to_read);
		*out_read += size_to_read;
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

	return CPM_SUCCESS;
}

static int write_sector(struct cpm_fs *fs,
			uint32_t c,
			uint32_t h,
			uint32_t s,
			const uint8_t *buf,
			size_t count,
			size_t offset)
{
	int ret;

	/* Trying to write more than a sector size, should not happen */
	if (count + offset > fs->attr.sector_size)
		return 1;

	/* If we're writing in the middle of a sector, read the existing data
	 * beforehand as not to overwrite the data at the start. */
	if (offset)
		if (fs->read_sector(fs->userdata, c, h, s, fs->cache) != 0)
			return CPM_ERR_SECTOR_READ;

	memcpy(fs->cache + offset, buf, count);
	ret = fs->write_sector(fs->userdata, c, h, s, fs->cache);

	return (ret == 0 ? 0 : -CPM_ERR_SECTOR_WRITE);
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static ssize_t write_block(struct cpm_fs *fs,
			   uint16_t block,
			   uint32_t offset,
			   uint8_t *buf,
			   size_t count)
{
	size_t written = 0;
	uint32_t c, h, s;
	uint32_t to_write;
	int ret;

	while (written < MIN(count, fs->attr.block_size - offset)) {
		block_to_chs(fs, block, written + offset, &c, &h, &s);

		to_write = MIN(fs->attr.sector_size, count - written);
		if (offset > 0 && written == 0) {
			/* Only apply offset to the first sector */
			uint32_t sector_off = offset % fs->attr.sector_size;
			to_write = MIN(to_write,
				       fs->attr.sector_size - sector_off);
			ret = write_sector(
				fs, c, h, s, buf, to_write, sector_off);
		} else {
			ret = write_sector(fs, c, h, s, buf, to_write, 0);
		}

		if (ret != 0)
			return -CPM_ERR_SECTOR_WRITE;

		written += to_write;
		buf = (uint8_t *)buf + to_write;
	}
	return (ssize_t)written;
}

enum cpm_fs_status cpm_fs_write(struct cpm_fs *fs,
				struct cpm_fs_file_handle *file,
				uint8_t *buf,
				size_t count,
				size_t *out_written)
{
	cpm_entry *entry;
	uint16_t block;
	ssize_t ret;
	size_t to_write;

	if (!fs || !file || !buf || !fs->write_sector || !out_written)
		return CPM_ERR_INVALID_ARG;

	if (file->mode & CPM_MODE_RDONLY)
		return CPM_ERR_FILE_READ_ONLY;

	entry = &fs->superblock.entries[file->entry];

	*out_written = 0;
	while (*out_written < count) {
		if (fs->block_addressing == CPM_BLOCK_ADDR_8)
			block = entry->block_ptr[file->block];
		else
			block = entry->block_ptr_w[file->block];

		/* Write to current block until it's full*/
		if (block > 0 && file->offset < fs->attr.block_size) {
			to_write = MIN(count - *out_written,
				       fs->attr.block_size - file->offset);
			ret = write_block(fs,
					  block,
					  file->offset,
					  (uint8_t *)buf + *out_written,
					  to_write);
			if (ret < 0)
				return -ret;
			*out_written += ret;
			file->offset += (uint32_t)ret;
		}

		/* Update record count to indicate file size */
		uint32_t bytes_in_extent =
			(file->offset + file->block * fs->attr.block_size) %
			0x4000;
		entry->rc = (uint8_t)((bytes_in_extent + 127) / 128);

		/* Done? */
		if (*out_written >= count)
			break;

		/* Next block */
		if (block == 0 || file->offset >= fs->attr.block_size) {
			if (block != 0)
				file->block += 1;
			file->offset = 0;

			if (file->block >= max_blocks_per_entry(fs)) {
				/* Physical extent full, allocate new one */
				int new_entry_idx = alloc_new_extent(fs, entry);
				if (new_entry_idx < 0)
					break;

				file->entry = (uint32_t)new_entry_idx;
				file->block = 0;
				file->offset = 0;
				entry = &fs->superblock.entries[file->entry];
			}

			/* Update number when starting a new logical extent  */
			if (file->block != 0 &&
			    file->block % (0x4000 / fs->attr.block_size) == 1)
				set_extent_nb(entry, extent_nb(entry) + 1);

			/* Allocate new block */
			uint16_t new_block = find_free_block(fs);
			if (!new_block)
				return CPM_ERR_DISK_FULL;
			av_set(fs, new_block);
			entry_set_block(fs, entry, file->block, new_block);
		}
	}

	return CPM_SUCCESS;
}

#undef MIN

enum cpm_fs_status
cpm_fs_unlink(struct cpm_fs *fs, const char *filename, int user)
{
	int32_t entry_idx;
	uint8_t header[13];
	int ret;

	if (!fs || !filename)
		return CPM_ERR_INVALID_ARG;

	if (!is_valid_user(user))
		return CPM_ERR_INVALID_USER;

	entry_idx = find_file(fs, filename, user);
	if (entry_idx == -1)
		return CPM_ERR_FILE_NOT_FOUND;

	/* user + filename + type */
	memcpy(header, &fs->superblock.entries[entry_idx], 12);
	for (uint32_t i = 0; i < fs->superblock.count; ++i)
		if (memcmp(header, &fs->superblock.entries[i], 12) == 0)
			wipe_extent(fs, i);

	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_rename(struct cpm_fs *fs,
				 const char *old_path,
				 int old_user,
				 const char *new_path,
				 int new_user)
{
	uint8_t header[13];
	char filename[8];
	char *parsed_file;
	char ext[3];
	char *parsed_ext;
	size_t filename_len;
	size_t ext_len;
	int entry_idx;
	int ret;

	if (!fs || !old_path || !new_path)
		return CPM_ERR_INVALID_ARG;
	if (!is_valid_user(old_user) || !is_valid_user(new_user))
		return CPM_ERR_INVALID_USER;

	memset(filename, 0x20, 8);
	memset(ext, 0x20, 3);

	/* Stop if destination already exists */
	entry_idx = find_file(fs, new_path, new_user);
	if (entry_idx != -1)
		return CPM_ERR_DESTINATION_EXISTS;

	/* Validate and separate path */
	ret = parse_filename(
		new_path, &parsed_file, &filename_len, &parsed_ext, &ext_len);
	if (ret != 0)
		return ret;

	memcpy(filename, parsed_file, filename_len);
	memcpy(ext, parsed_ext, ext_len);

	/* Find origin */
	entry_idx = find_file(fs, old_path, old_user);
	if (entry_idx == -1)
		return CPM_ERR_FILE_NOT_FOUND;

	/* Copy flags */
	for (int i = 0; i < 3; ++i)
		ext[i] =
			(fs->superblock.entries[entry_idx].extension[i] & 0x40);

	/* user + filename + type */
	memcpy(header, &fs->superblock.entries[entry_idx], 12);
	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		cpm_entry *entry = &fs->superblock.entries[i];
		if (memcmp(header, entry, 12) == 0) {
			memcpy(entry->file, filename, 8);
			memcpy(entry->extension, ext, 3);
		}
	}

	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_get_available_space(struct cpm_fs *fs,
					      size_t *out_space)
{
	uint16_t total_blocks;
	size_t available_space = 0;

	if (!fs || !out_space)
		return CPM_ERR_INVALID_ARG;

	total_blocks = fs->disk_size / fs->attr.block_size;
	for (uint16_t i = 0; i < total_blocks; ++i)
		if (av_get(fs, i) == 0)
			available_space += fs->attr.block_size;

	*out_space = available_space;
	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_close(struct cpm_fs *fs,
				struct cpm_fs_file_handle *file_handle)
{
	if (!fs || !file_handle)
		return CPM_ERR_INVALID_ARG;

	free(file_handle);
	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_readdir(struct cpm_fs *fs,
				  struct cpm_fs_dir *dirp,
				  struct cpm_fs_file **out_file)
{
	cpm_entry *entry;
	uint8_t *filename_out;
	int i;

	if (!fs || !dirp || !out_file)
		return CPM_ERR_INVALID_ARG;

	while ((uint32_t)++dirp->current_file_ino < fs->superblock.count) {
		/* Iterate until we find a file entry */
		if (cpm_entry_is_valid(
			    &fs->superblock.entries[dirp->current_file_ino]) &&
		    entry_is_first_extent(fs, dirp->current_file_ino))
			break;
	}
	if ((uint32_t)dirp->current_file_ino >= fs->superblock.count) {
		/* Done iterating through all entries */
		*out_file = NULL;
		return CPM_SUCCESS;
	}

	entry = &fs->superblock.entries[dirp->current_file_ino];

	/* Copy file name without status flags */
	filename_out = (uint8_t *)dirp->file.d_name;
	memset(&dirp->file, 0, sizeof(struct cpm_fs_file));
	i = -1;
	while (++i < 8 && (entry->file[i] & 0x7f) != 0x20)
		*(filename_out++) = entry->file[i] & 0x7f;
	if ((entry->extension[0] & 0x7f) != 0x20) {
		*(filename_out++) = '.';
		i = -1;
		while (++i < 3 && (entry->extension[i] & 0x7f) != 0x20)
			*(filename_out++) = entry->extension[i] & 0x7f;
	}

	/* File attributes, CP/M >= 2.0 */
	if (F_IS_READONLY(entry))
		dirp->file.d_flags |= CPM_FS_FLAG_READONLY;
	if (F_IS_SYSTEMFILE(entry))
		dirp->file.d_flags |= CPM_FS_FLAG_SYSTEM;
	if (F_IS_ARCHIVED(entry))
		dirp->file.d_flags |= CPM_FS_FLAG_ARCHIVED;

	dirp->file.d_user = entry->status & 0x0F;
	dirp->file.d_size = get_filesize(fs, entry);

	*out_file = &dirp->file;
	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_closedir(struct cpm_fs __attribute__((unused)) * fs,
				   struct cpm_fs_dir *dir)
{
	if (!dir)
		return CPM_ERR_INVALID_ARG;
	free(dir);
	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_getattr(struct cpm_fs *fs,
				  struct cpm_fs_file_handle *file,
				  int *out_attrs)
{
	if (!fs || !file || !out_attrs)
		return CPM_ERR_INVALID_ARG;

	*out_attrs = 0;
	cpm_entry *entry = &fs->superblock.entries[file->entry];
	if (F_IS_READONLY(entry))
		*out_attrs |= CPM_FS_FLAG_READONLY;
	if (F_IS_SYSTEMFILE(entry))
		*out_attrs |= CPM_FS_FLAG_SYSTEM;
	if (F_IS_ARCHIVED(entry))
		*out_attrs |= CPM_FS_FLAG_ARCHIVED;

	return CPM_SUCCESS;
}

enum cpm_fs_status
cpm_fs_setattr(struct cpm_fs *fs, struct cpm_fs_file_handle *file, int attrs)
{
	if (!fs || !file || !attrs)
		return CPM_ERR_INVALID_ARG;

	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		/* Compare user, filename, ext */
		cpm_entry *entry = &fs->superblock.entries[i];
		if (memcmp(entry, &fs->superblock.entries[file->entry], 12) ==
		    0) {
			if (attrs & CPM_FS_FLAG_READONLY)
				F_SET_READONLY(entry);
			if (attrs & CPM_FS_FLAG_SYSTEM)
				F_SET_SYSTEMFILE(entry);
			if (attrs & CPM_FS_FLAG_ARCHIVED)
				F_SET_ARCHIVED(entry);
		}
	}

	return CPM_SUCCESS;
}

static void invert_superblock_endianness(struct cpm_fs *fs)
{
	uint16_t tmp;
	for (uint32_t i = 0; i < fs->superblock.count; ++i) {
		tmp = fs->superblock.entries[i].block_ptr_w[i];
		fs->superblock.entries[i].block_ptr_w[i] =
			((tmp & 0xFF00) >> 8) | ((tmp & 0x00FF) << 8);
	}
}

/* Store superblock and parse directory entries */
static int read_superblock(struct cpm_fs *fs)
{
	int i = 0, j = 0;
	uint32_t c, h, s;
	struct cpm_superblock *sb = &fs->superblock;
	uint32_t entries_c = fs->attr.sector_size / sizeof(cpm_entry);
	int ret;

	/* Locate superblock */
	block_to_chs(fs, 0, 0, &c, &h, &s);

	sb->count = fs->attr.max_dir_entries;
	sb->entries = (cpm_entry *)malloc(sizeof(cpm_entry) * sb->count);
	if (!sb->entries)
		return CPM_ERR_NOMEM;

	while ((uint32_t)i < sb->count) {
		ret = fs->read_sector(fs->userdata, c, h, s, fs->cache);
		if (ret != 0)
			return CPM_ERR_SECTOR_READ;

		for (j = 0; j < entries_c && i + j < sb->count; ++j)
			memcpy(&sb->entries[i + j],
			       fs->cache + j * sizeof(cpm_entry),
			       sizeof(cpm_entry));

		i += j;
		block_to_chs(fs, 0, (i * sizeof(cpm_entry)), &c, &h, &s);
	}

	/* Available disk size for extents */
	fs->disk_size = get_disk_size(fs);
	if (fs->disk_size <= 256 * fs->attr.block_size)
		fs->block_addressing = CPM_BLOCK_ADDR_8;
	else
		fs->block_addressing = CPM_BLOCK_ADDR_16;

	/* CP/M headers use little endian. Adapt if host is big endian. */
	if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		invert_superblock_endianness(fs);

	return 0;
}

#include <stdio.h>

static enum cpm_fs_status set_skew_settings(struct cpm_fs *fs,
					    struct cpm_fs_attr *attributes)
{
	if (attributes->skew_table == NULL) {
		/* No skew table */
		fs->attr.skew_table = NULL;
		return CPM_SUCCESS;
	}

	fs->attr.skew_table =
		calloc(sizeof(uint32_t) * attributes->sector_count, 1);
	if (fs->attr.skew_table == NULL)
		return CPM_ERR_NOMEM;

	for (uint32_t i = 0; i < fs->attr.sector_count; ++i)
		fs->attr.skew_table[attributes->skew_table[i] - 1] = i + 1;
	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_new(struct cpm_fs_attr *attributes,
			      read_sector_cb get_sector_cb,
			      write_sector_cb set_sector_cb,
			      void *userdata,
			      struct cpm_fs **out)
{
	struct cpm_fs *fs;
	int err = 0;

	if (!get_sector_cb || !attributes || !out)
		return CPM_ERR_INVALID_ARG;

	fs = (struct cpm_fs *)calloc(sizeof(struct cpm_fs), 1);
	if (!fs)
		return CPM_ERR_NOMEM;

	fs->attr = *attributes;
	fs->attr.skew_table = NULL;
	if ((err = set_skew_settings(fs, attributes)))
		goto error;

	fs->read_sector = get_sector_cb;
	fs->write_sector = set_sector_cb;
	fs->userdata = userdata;
	fs->cache = (uint8_t *)calloc(fs->attr.sector_size, 1);
	if (!fs->cache) {
		err = -CPM_ERR_NOMEM;
		goto error;
	}

	if ((err = read_superblock(fs)))
		goto error;

	if ((err = check_superblock(fs)))
		goto error;

	if ((err = av_build(fs)))
		goto error;

	*out = fs;

	return CPM_SUCCESS;
error:
	cpm_fs_destroy(fs);
	*out = NULL;
	return err;
}

enum cpm_fs_status cpm_fs_destroy(struct cpm_fs *fs)
{
	if (!fs)
		return CPM_ERR_INVALID_ARG;
	free(fs->superblock.entries);
	free(fs->attr.skew_table);
	free(fs->cache);
	free(fs->av);
	free(fs);

	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_sync(struct cpm_fs *fs)
{
	if (!fs)
		return CPM_ERR_INVALID_ARG;

	return write_superblock(fs);
}

const char *cpm_fs_status_str(enum cpm_fs_status status)
{
	switch (status) {
	case CPM_SUCCESS:
		return "Success";
	case CPM_ERR_INVALID_ARG:
		return "Invalid argument";
	case CPM_ERR_NOMEM:
		return "Out of memory";
	case CPM_ERR_BLOCK_OVERFLOW:
		return "Geometry error: "
		       "file entry points to block outside the disk capacity";
	case CPM_ERR_FILE_OVERLAP:
		return "Geometry error: two files point to the same block";
	case CPM_ERR_FILE_DIR_OVERLAP:
		return "Geometry error: file blocks overlap with directory";
	case CPM_ERR_SECTOR_READ:
		return "Error when reading sector";
	case CPM_ERR_SECTOR_WRITE:
		return "Error when writing sector";
	case CPM_ERR_FILE_ALREADY_EXISTS:
		return "File already exists";
	case CPM_ERR_FILE_NOT_FOUND:
		return "Unable to find file";
	case CPM_ERR_FILENAME_INVALID:
		return "Invalid filename";
	case CPM_ERR_DISK_FULL:
		return "Disk is full";
	case CPM_ERR_INVALID_USER:
		return "Invalid user number";
	case CPM_ERR_FILE_READ_ONLY:
		return "Trying to write to a file opened as read-only";
	case CPM_ERR_DESTINATION_EXISTS:
		return "Trying to rename a file to a name that already exists";
	default:
		return "Unknown status code";
	}
}
