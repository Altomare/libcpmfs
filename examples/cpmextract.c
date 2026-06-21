#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
	.boot_cylinders = 0,
	.skip_first_cylinders = 3,
};

/* For CHS raw sector images */
static int get_sector(void *userdata,
		      uint32_t cylinder,
		      uint32_t head,
		      uint32_t sector, /* Start at 1 */
		      uint8_t *out_sector)
{
	disk_image *disk = (disk_image *)userdata;
	uint32_t cylinder_size = disk->sector_count * disk->sector_size;
	int ret;
	uint32_t offset = 0;

	if (!disk || !disk->handle)
		return -EINVAL;

	offset += cylinder * cylinder_size * 2;
	offset += (sector - 1) * disk->sector_size;
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

static void dump_file(struct cpm_fs *fs, struct cpm_fs_file *cpmfile)
{
	struct cpm_fs_file_handle *f;
	char new_name[256];
	uint8_t buf[513];
	size_t read_bytes;
	int new_f;
	enum cpm_fs_status status;

	memcpy(new_name, cpmfile->d_name, 16);
	for (int i = 0; i < 256; ++i) {
		if (new_name[i] == '/')
			new_name[i] = '_';
		if (new_name[i] == ':')
			new_name[i] = '.';
	}

	printf("%d:%s -> %s\n", cpmfile->d_user, cpmfile->d_name, new_name);

	if (access(new_name, F_OK) == 0) {
		fprintf(stderr, "\t%s already exists.\n", new_name);
		return;
	}

	new_f = open(new_name,
		     O_CREAT | O_RDWR,
		     S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (!new_f) {
		fprintf(stderr, "\tUnable to open %s.\n", new_name);
		return;
	}
	status = cpm_fs_open(
		fs, cpmfile->d_name, CPM_MODE_RDONLY, cpmfile->d_user, &f);
	if (!f) {
		fprintf(stderr, "\t[CPM] Unable to open %s\n", cpmfile->d_name);
		return;
	}

	status = cpm_fs_read(fs, f, buf, 512, &read_bytes);
	while (status == CPM_SUCCESS && read_bytes > 0) {
		write(new_f, buf, read_bytes);
		status = cpm_fs_read(fs, f, buf, 512, &read_bytes);
	}
	if (status != CPM_SUCCESS)
		fprintf(stderr, "Read error: %s\n", cpm_fs_status_str(status));

	cpm_fs_close(fs, f);
	close(new_f);
}


static int list_and_dump(const char *file)
{
	struct cpm_fs_file *cpmfile;
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
	if (status != CPM_SUCCESS)
		goto end;

	status = cpm_fs_opendir(fs, &dirp);
	if (status != CPM_SUCCESS)
		goto end;

	status = cpm_fs_readdir(fs, dirp, &cpmfile);
	while (status == CPM_SUCCESS && cpmfile) {
		if (cpmfile->d_size > 0)
			dump_file(fs, cpmfile);
		else
			printf("Skipped empty file %s\n", cpmfile->d_name);
		status = cpm_fs_readdir(fs, dirp, &cpmfile);
	}
	cpm_fs_closedir(fs, dirp);

	cpm_fs_destroy(fs);

end:
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
	if (list_and_dump(argv[1]))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
