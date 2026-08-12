/* Copyright (c) 2025 Arthur DAUZAT
 * SPDX-License-Identifier: BSD-3-Clause */

#include <string.h>

#include "cpmfs_internal.h"

enum cpm_fs_status cpm_fs_get_init_crawler(struct cpm_fs *fs,
					   struct cpm_fs_crawler **out_crawler)
{
	struct cpm_fs_crawler *res;

	if (!fs || !out_crawler)
		return CPM_ERR_INVALID_ARG;

	res = calloc(sizeof(struct cpm_fs_crawler), 1);
	if (!res)
		return CPM_ERR_NOMEM;

	res->buf = calloc(fs->attr.block_size + 1, 1);
	if (!res->buf) {
		free(res);
		return CPM_ERR_NOMEM;
	}

	*out_crawler = res;
	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_get_unused_blocks(struct cpm_fs *fs,
					    struct cpm_fs_crawler *crawler,
					    uint8_t **out_buf)
{
	uint32_t max_blocks = fs->disk_size / fs->attr.block_size;
	uint32_t c, h, s;
	int ret;

	if (!fs || !crawler || !out_buf)
		return CPM_ERR_INVALID_ARG;

	for (uint32_t i = crawler->block; i < max_blocks; ++i) {
		if (av_get(fs, i))
			continue;

		for (uint32_t j = 0;
		     j < fs->attr.block_size / fs->attr.sector_size;
		     ++j) {
			block_to_chs(
				fs, i, j * fs->attr.sector_size, &c, &h, &s);
			ret = fs->read_sector(fs->userdata, c, h, s, fs->cache);
			if (ret != 0)
				return CPM_ERR_SECTOR_READ;

			memcpy(crawler->buf + j * fs->attr.sector_size,
			       fs->cache,
			       fs->attr.sector_size);
		}
		crawler->block = i + 1;
		*out_buf = crawler->buf;
		return CPM_SUCCESS;
	}
	*out_buf = NULL;
	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_get_destroy_crawler(struct cpm_fs *fs,
					      struct cpm_fs_crawler *crawler)
{
	if (!fs || !crawler)
		return CPM_ERR_INVALID_ARG;
	free(crawler->buf);
	free(crawler);
	return CPM_SUCCESS;
}

enum cpm_fs_status cpm_fs_wipe_unused_sectors(struct cpm_fs *fs)
{
	uint32_t max_blocks = fs->disk_size / fs->attr.block_size;
	uint32_t c, h, s;
	int ret;

	if (!fs)
		return CPM_ERR_INVALID_ARG;

	memset(fs->cache, 0, fs->attr.sector_size);

	for (uint32_t i = 0; i < max_blocks; ++i) {
		if (av_get(fs, i))
			continue;

		for (uint32_t j = 0;
		     j < fs->attr.block_size / fs->attr.sector_size;
		     ++j) {
			block_to_chs(
				fs, i, j * fs->attr.sector_size, &c, &h, &s);
			ret = fs->write_sector(
				fs->userdata, c, h, s, fs->cache);
			if (ret != 0)
				return CPM_ERR_SECTOR_WRITE;
		}
	}
	// TODO: Wipe end of used blocks by checking RC
	return CPM_SUCCESS;
}
