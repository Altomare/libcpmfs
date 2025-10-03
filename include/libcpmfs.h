/* Copyright (c) 2025 Arthur DAUZAT
 * SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#define LIBCPMFS_VERSION 0.1

/* Notes:
 *   Skew and interleave settings are not needed, as the sector numbering is
 *   given by the floppy driver.
 */

enum cpm_fs_status {
	CPM_SUCCESS = 0,
	/* Invalid argument */
	CPM_ERR_INVALID_ARG,
	/* Memory allocation error */
	CPM_ERR_NOMEM,
	/* A file points to a block outside of the disk max capacity */
	CPM_ERR_BLOCK_OVERFLOW,
	/* Multiple files point to the same block */
	CPM_ERR_FILE_OVERLAP,
	/* File location would overlap with the directory table */
	CPM_ERR_FILE_DIR_OVERLAP,
};

struct cpm_fs_attr {
	/* Disk geometry */
	uint32_t cylinders;
	uint32_t heads;
	uint32_t sector_count;
	uint32_t sector_size;

	/* Filesystem attributes */
	uint32_t block_size;
	uint32_t max_dir_entries;

	/* Reserved cylinders, mutually exclusive.
	 * They are still counted in the cylinders field. */
	uint32_t boot_cylinders; /* On every head */
	uint32_t skip_first_cylinders; /* On first head */
};

#define CPM_FS_FLAG_SYSTEM 0x1
#define CPM_FS_FLAG_READONLY 0x2
#define CPM_FS_FLAG_ARCHIVED 0x4

struct cpm_fs_file {
	/* 256 for POSIX compatibility, but real size is limited to 12 */
	char d_name[256];
	ino_t d_ino;
	uint32_t d_size;
	uint8_t d_user;
	uint8_t d_flags;
};

/* Returns 0 for success.
 * Note: cylinder & head numbering start at 0. sector starts at 1. */
typedef int (*get_sector_cb)(void *userdata,
			     uint32_t cylinder,
			     uint32_t head,
			     uint32_t sector,
			     uint8_t *out_sector);

/* Opaque */
struct cpm_fs;
struct cpm_fs_dir;
struct cpm_fs_file_handle;

enum cpm_fs_status cpm_fs_new(struct cpm_fs_attr *attributes,
			      get_sector_cb sector_cb,
			      void *userdata,
			      struct cpm_fs **out);
void cpm_fs_destroy(struct cpm_fs *fs);

struct cpm_fs_dir *cpm_fs_opendir(struct cpm_fs *fs);
struct cpm_fs_file *cpm_fs_readdir(struct cpm_fs *fs, struct cpm_fs_dir *dirp);
void cpm_fs_closedir(struct cpm_fs_dir *dir);

struct cpm_fs_file_handle *
cpm_fs_open(struct cpm_fs *fs, const char *pathname, int user);
ssize_t cpm_fs_read(struct cpm_fs *fs,
		    struct cpm_fs_file_handle *file,
		    void *buf,
		    size_t count);
void cpm_fs_close(struct cpm_fs *fs, struct cpm_fs_file_handle *file);
