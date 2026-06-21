/* Copyright (c) 2025 Arthur DAUZAT
 * SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define LIBCPMFS_VERSION 0.2

enum cpm_fs_status {
	CPM_SUCCESS = 0,
	/* Invalid argument */
	CPM_ERR_INVALID_ARG,
	/* Memory allocation error */
	CPM_ERR_NOMEM,
	/* A file points to a block outside of the disk max capacity */
	CPM_ERR_BLOCK_OVERFLOW,
	/* Multiple files point to the same block
	 * This error was intended to spot bad geometries, but this can also
	 * happen for normal reasons. Programs meant to locate bad sectors
	 * usually create a file pointing to all these bad sectors.
	 * These include FINDBAD (creates [UNUSED].BAD), BADLIM, RECLAIM... */
	CPM_ERR_FILE_OVERLAP,
	/* File location would overlap with the directory table */
	CPM_ERR_FILE_DIR_OVERLAP,
	/* Error in reading sector callback */
	CPM_ERR_SECTOR_READ,
	/* Error in writing sector callback */
	CPM_ERR_SECTOR_WRITE,
	/* Trying to create an file that already exists */
	CPM_ERR_FILE_ALREADY_EXISTS,
	/* Cannot find given filename */
	CPM_ERR_FILE_NOT_FOUND,
	/* Filename does not respect CP/M naming rules */
	CPM_ERR_FILENAME_INVALID,
	/* No space left on disk */
	CPM_ERR_DISK_FULL,
	/* Invalid user, must be between 0 and 15 */
	CPM_ERR_INVALID_USER,
	/* Trying to write to a file opened in read-only mode */
	CPM_ERR_FILE_READ_ONLY,
	/* Trying to rename a file to a name that already exists */
	CPM_ERR_DESTINATION_EXISTS,
};

enum cpm_fs_mode {
	CPM_MODE_WRONLY,
	CPM_MODE_RDONLY,
	CPM_MODE_RDWR,
};

struct cpm_fs_attr {
	/* Disk geometry */
	uint32_t cylinders;
	uint32_t heads;
	uint32_t sector_count;
	uint32_t sector_size;

	/* Filesystem attributes */
	uint32_t block_size; /* 1024, 2048, 4096, 8192 or 16384 */
	uint32_t max_dir_entries;

	/* Reserved cylinders, mutually exclusive.
	 * They are still counted in the cylinders field. */
	uint32_t boot_cylinders; /* On every head */
	uint32_t skip_first_cylinders; /* On first head */

	/* Skew and interleave settings are not needed here, as sector
	 * numbering is given by the floppy driver. */
};

#define CPM_FS_FLAG_SYSTEM 0x1
#define CPM_FS_FLAG_READONLY 0x2
#define CPM_FS_FLAG_ARCHIVED 0x4

struct cpm_fs_file {
	/* 256 for POSIX compatibility, but real size is limited to 12.
	 * 8 characters for filename, 1 for dot, 3 for extension. */
	char d_name[256];
	uint32_t d_size;
	uint8_t d_user;
	uint8_t d_flags;
};

/* Returns 0 for success.
 * in_sector & out_sector should match the sector_size specified in cpm_fs_attr.
 * Note: cylinder & head numbering start at 0. sector starts at 1. */
typedef int (*read_sector_cb)(void *userdata,
			      uint32_t cylinder,
			      uint32_t head,
			      uint32_t sector,
			      uint8_t *out_sector);
typedef int (*write_sector_cb)(void *userdata,
			       uint32_t cylinder,
			       uint32_t head,
			       uint32_t sector,
			       uint8_t *in_sector);

/* Opaque */
struct cpm_fs;
struct cpm_fs_dir;
struct cpm_fs_file_handle;

/* Every function returns 0 (CPM_SUCCESS) on success, or negative cpm_fs_status
 * on error. */

/* File names are case-sensitive. The CCP (command line interface) forces
 * upper case, so almost every file is in uppercase. However, some software
 * (MBASIC for instance) can create and modify lower case filenames. */

int cpm_fs_new(struct cpm_fs_attr *attributes,
	       read_sector_cb get_sector_cb,
	       write_sector_cb set_sector_cb,
	       void *userdata,
	       struct cpm_fs **out);
int cpm_fs_destroy(struct cpm_fs *fs);

/* Directory, no name argument needed as there are no subdirectories */
int cpm_fs_opendir(struct cpm_fs *fs, struct cpm_fs_dir **out_dir);
int cpm_fs_readdir(struct cpm_fs *fs,
		   struct cpm_fs_dir *dirp,
		   struct cpm_fs_file **out_file);
int cpm_fs_closedir(struct cpm_fs *fs, struct cpm_fs_dir *dir);

/* Open the given file.
 * Opening a file twice at the same time will cause undefined behavior. */
int cpm_fs_open(struct cpm_fs *fs,
		const char *pathname,
		enum cpm_fs_mode mode,
		int user,
		struct cpm_fs_file_handle **out_file);
int cpm_fs_close(struct cpm_fs *fs, struct cpm_fs_file_handle *file);

/* Read bytes from opened file into buf.
 * Number of read bytes is written to out_read. */
int cpm_fs_read(struct cpm_fs *fs,
		struct cpm_fs_file_handle *file,
		uint8_t *buf,
		size_t count,
		size_t *out_read);

int cpm_fs_write(struct cpm_fs *fs,
		 struct cpm_fs_file_handle *file,
		 uint8_t *buf,
		 size_t count,
		 size_t *out_written);

/* Attributes */
int cpm_fs_getattr(struct cpm_fs *fs,
		   struct cpm_fs_file_handle *file,
		   int *out_attrs);
int cpm_fs_setattr(struct cpm_fs *fs,
		   struct cpm_fs_file_handle *file,
		   int attrs);

/* Delete file from disk. The blocks containing the file contents are not wiped
 * but marked available for other files again.. */
int cpm_fs_unlink(struct cpm_fs *fs, const char *pathname, int user);

/* Renaming a currently opened file may cause undefined behavior */
int cpm_fs_rename(struct cpm_fs *fs,
		  const char *old_path,
		  int old_user,
		  const char *new_path,
		  int new_user);

/* Error code to printable string */
const char *cpm_fs_status_str(enum cpm_fs_status status);

#ifdef __cplusplus
}
#endif
