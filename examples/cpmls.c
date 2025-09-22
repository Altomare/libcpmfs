#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "libcpmfs.h"

typedef struct _disk_image
{
        /* Disk geometry */
        uint32_t sector_size;
        uint32_t sector_count;
        uint32_t heads;

        FILE *handle;
} disk_image;

static struct cpm_fs_attr otronafs = {
        .sector_size = 512,
        .sector_count = 10,
        .cylinders = 40,
        .heads = 2,
        .block_size = 2048,
        .boot_tracks = 3,
        .max_dir_entries = 128,
};

/* For CHS raw sector images */
static int get_sector(void *userdata,
                      uint32_t cylinder,
                      uint32_t head,
                      uint32_t sector, /* Start at 1 */
                      uint8_t *out_sector)
{
        disk_image *disk = (disk_image *)userdata;
        uint32_t track_size = disk->sector_count * disk->sector_size;
        int ret;
        uint32_t offset = 0;

        if (!disk || !disk->handle)
                return -EINVAL;

        offset += cylinder * track_size * 2;
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

static int cpmls(const char *file)
{
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

        status = cpm_fs_new(&otronafs, &get_sector, &img, &fs);
        if (status)
                goto end;

        struct cpm_fs_dir *dirp = cpm_fs_opendir(fs);

        struct cpm_fs_file *cpmfile;
        while ((cpmfile = cpm_fs_readdir(fs, dirp)))
		printf("%12s [%d][%c%c%c][%u bytes]\n",
		       cpmfile->d_name,
		       cpmfile->d_user,
		       cpmfile->d_flags & CPM_FS_FLAG_SYSTEM ? 'S' : ' ',
		       cpmfile->d_flags & CPM_FS_FLAG_READONLY ? 'R' : ' ',
		       cpmfile->d_flags & CPM_FS_FLAG_ARCHIVED ? 'A' : ' ',
                       cpmfile->d_size);
	cpm_fs_closedir(dirp);

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
        if (cpmls(argv[1]))
                return EXIT_FAILURE;
        return EXIT_SUCCESS;
}
