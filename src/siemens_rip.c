#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <libsimg.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t width;
    uint8_t height;
    uint16_t type;
    uint32_t bitmap;
} SIMG_CLASSIC;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t type;
    uint32_t bitmap;
} SIMG_ELKA;
#pragma pack(pop)

typedef struct {
    uint16_t width;
    uint16_t height;
    enum SIMGType type;
} SIMG;

#define SIMG_CLASSIC_OFFSET_BITMAP 4
#define SIMG_ELKA_OFFSET_BITMAP 8

int VERBOSE;

uint8_t *load_ff_file(const char *path, size_t *size) {
    uint8_t *buffer = NULL;
    FILE *ff = fopen(path, "rb");
    if (ff) {
        fseek(ff, 0, SEEK_END);
        *size = ftell(ff);
        if (*size >= 32 * 1024 * 1024 && *size <= 96 * 1024 * 1024) {
            fseek(ff, 0, SEEK_SET);
            buffer = malloc(*size);
            if (buffer) {
                if (fread(buffer, 1, *size, ff) != *size) {
                    free(buffer);
                    buffer = NULL;
                }
            }
        }
        fclose(ff);
    }
    return buffer;
}

void write_png(const char *dir_name, const uint8_t *pixels, int width, int height, int type, int id) {
    char path[PATH_MAX + 1];
    snprintf(path, sizeof(path), "%s/%d.png", dir_name, id);
    simg_write_png(path, pixels, width, height, type);
}

void rip(const char *dir_name, const uint8_t *buffer, size_t size) {
    int platform = -1;
    uint8_t *imghdr = simg_find_pit(buffer, size, &platform);
    if (imghdr) {
        int i = 0;
        while (imghdr) {
            SIMG simg;
            uint8_t *addr;
            int offset_bitmap;
            if (platform <= 1) { // SG or NSG
                SIMG_CLASSIC *simg_classic = (SIMG_CLASSIC *)imghdr;
                addr = (uint8_t *)simg_classic;
                offset_bitmap = SIMG_CLASSIC_OFFSET_BITMAP;
                simg.width = simg_classic->width;
                simg.height = simg_classic->height;
                simg.type = simg_classic->type;
            } else { // ELKA
                SIMG_ELKA *simg_elka = (SIMG_ELKA *)imghdr;
                addr = (uint8_t *)simg_elka;
                offset_bitmap = SIMG_ELKA_OFFSET_BITMAP;
                simg.width = simg_elka->width;
                simg.height = simg_elka->height;
                simg.type = simg_elka->type;
            }
            if (simg.width == 0 || simg.height == 0) {
                break;
            }
            uint32_t bitmap_offset = simg_addr_to_offset(addr + offset_bitmap);
            if (!bitmap_offset) {
                break;
            }
            const int bpp = simg_get_bpp_by_type(simg.type);
            if (!bpp) {
                break;
            }
            uint8_t *pixels = NULL;
            if (simg.type & 0x80) { // rle
                pixels = simg_unpack_rle(buffer + bitmap_offset, simg.width, simg.height, bpp);
                if (!pixels) {
                    break;
                }
                write_png(dir_name, pixels, simg.width, simg.height, simg.type, i);
            } else {
                write_png(dir_name, buffer + bitmap_offset, simg.width, simg.height, simg.type, i);
            }
            if (pixels) {
                free(pixels);
            }
            if (VERBOSE) {
                fprintf(stdout, "Extracted %d: %dx%d (bpp=%d)\n", i, simg.width, simg.height, bpp);
            }

            const uint32_t imghdr_offset = (platform <= 1) ? sizeof(SIMG_CLASSIC) : sizeof(SIMG_ELKA);
            if (imghdr + imghdr_offset > buffer + size) {
                break;
            }
            imghdr += imghdr_offset;

            i++;
        }
        if (i) {
            fprintf(stdout, "Extracted %d images\n", i);
        }
    } else {
        fprintf(stderr, "Error: could not find PIT\n");
    }
}

void get_dir_name(char *dest, const char *basename) {
    if (!dest || !basename) {
        return;
    }
    const char *e = strrchr(basename, '.');
    size_t len = (e) ? e - basename : strlen(basename);
    if (len > PATH_MAX) {
        len = PATH_MAX;
    }
    strncpy(dest, basename, len);
    dest[len] = '\0';
}

int create_dir(const char *path) {
    if (mkdir(path, 0755) == 0) return 1;
    if (errno == EEXIST) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    int opt;
    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    // ReSharper disable once CppDFALoopConditionNotUpdated

    while ((opt = getopt_long(argc, argv, "vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'v': VERBOSE = 1; break;
            case 'h':
                printf("Usage: %s [options] <fullflash.bin>\n", basename(argv[0]));
                printf("Options:\n");
                printf("  -v, --verbose   enable verbose output\n");
                printf("  -h, --help      show this help\n");
                return 0;
            default: return 1;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "Error: fullflash file required\n");
        return 1;
    }
    if (strlen(argv[optind]) >= PATH_MAX) {
        fprintf(stderr, "Error: fullflash file name too long\n");
        return 1;
    }

    char ff_path[PATH_MAX + 1], dir_name[PATH_MAX + 1];
    snprintf(ff_path, sizeof(ff_path), "%s", argv[optind]);
    const char *ff_name = strrchr(ff_path, '/');
    ff_name = ff_name ? ff_name + 1 : ff_path;

    size_t size;
    uint8_t *buffer = load_ff_file(ff_path, &size);
    if (buffer) {
        get_dir_name(dir_name, ff_name);
        if (create_dir(dir_name)) {
            rip(dir_name, buffer, size);
        } else {
            fprintf(stderr, "Error: could not create directory %s\n", dir_name);
            free(buffer);
            return 1;
        }
        free(buffer);
    } else {
        fprintf(stderr, "Error: could not load fullflash file\n");
        return 1;
    }
    return 0;
}