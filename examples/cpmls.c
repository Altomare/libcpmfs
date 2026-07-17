#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libcpmfs.h"

typedef struct _disk_image {
	/* Disk geometry */
	uint32_t sector_size;
	uint32_t sector_count;
	uint32_t heads;

	FILE *handle;
} disk_image;

static struct cpm_fs_attr otronafs = {
	.cylinders = 40,
	.heads = 2,
	.sector_count = 10,
	.sector_size = 512,
	.block_size = 2048,
	.max_dir_entries = 128,
	.boot_cylinders = 3,
	.fill_order = CPM_FILL_HCS,
};

/* For CHS raw sector images */
static int get_sector(void *userdata,
		      uint32_t cylinder,
		      uint32_t head,
		      uint32_t sector,
		      uint8_t *out_sector)
{
	disk_image *disk = (disk_image *)userdata;
	uint32_t cylinder_size = disk->sector_count * disk->sector_size;
	int ret;
	uint32_t offset = 0;

	if (!disk || !disk->handle)
		return -EINVAL;

	offset += cylinder * cylinder_size * 2;
	offset += sector * disk->sector_size;
	if (head == 1)
		offset += disk->sector_count * disk->sector_size;
	ret = fseek(disk->handle, offset, SEEK_SET);
	if (ret != 0)
		return -errno;

	ret = (int)fread(out_sector, 1, disk->sector_size, disk->handle);
	if ((uint32_t)ret != disk->sector_size) {
		fprintf(stderr, "Read error\n");
		return -EIO;
	}

	return 0;
}

static int cpmls(const char *file)
{
	struct cpm_fs_dir *dirp;
	disk_image img;
	struct cpm_fs *fs;
	int status;

	img.handle = fopen(file, "rb");
	if (!img.handle) {
		fprintf(stderr, "Unable to open %s", file);
		return ENOENT;
	}

	img.sector_size = otronafs.sector_size;
	img.sector_count = otronafs.sector_count;
	img.heads = otronafs.heads;

	status = cpm_fs_new(&otronafs, &get_sector, NULL, &img, &fs);
	if (status)
		goto end;

	status = cpm_fs_opendir(fs, &dirp);
	if (status != CPM_SUCCESS)
		goto end;

	struct cpm_fs_file *cpmfile;
	cpm_fs_readdir(fs, dirp, &cpmfile);
	while (cpmfile) {
		printf("%12s [%d][%c%c%c][%u bytes]\n",
		       cpmfile->d_name,
		       cpmfile->d_user,
		       cpmfile->d_flags & CPM_FS_FLAG_SYSTEM ? 'S' : ' ',
		       cpmfile->d_flags & CPM_FS_FLAG_READONLY ? 'R' : ' ',
		       cpmfile->d_flags & CPM_FS_FLAG_ARCHIVED ? 'A' : ' ',
		       cpmfile->d_size);
		cpm_fs_readdir(fs, dirp, &cpmfile);
	}
	cpm_fs_closedir(fs, dirp);

	cpm_fs_destroy(fs);

end:
	if (status != CPM_SUCCESS)
		fprintf(stderr, "libcpmfs: %s\n", cpm_fs_status_str(status));
	fclose(img.handle);
	return 0;
}

static void usage(const char *name)
{
	printf("usage: %s raw_file\n", name);
}

int main(int argc, const char *argv[])
{
	if (argc != 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if (cpmls(argv[1]))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
