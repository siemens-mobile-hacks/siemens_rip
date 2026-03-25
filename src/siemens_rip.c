#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
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

char OUT_DIR[256];

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
                fread(buffer, 1, *size, ff);
            }
        }
        fclose(ff);
    }
    return buffer;
}

int detect_platform(const uint8_t *buffer, size_t size) {
    uint8_t sig[] = {0x68, 0x77, 0x5F, 0x64, 0x65, 0xF4};
    uint8_t *found = memmem(buffer, size, sig, sizeof(sig));
    if (!found || found - buffer - 2 < 0) return -1;
    uint8_t byte = *(found - 2);
    if (byte == 0xDA) {
        return 0;
    } else if (byte == 0x26) {
        return 1;
    } else if (byte == 0xAE) {
        return 2;
    }
    return -1;
}

void write_png(const uint8_t *pixels, int width, int height, int type, int id) {
    char *path = malloc(PATH_MAX - strlen(OUT_DIR) - 8);
    sprintf(path, "%s//%d.png", OUT_DIR, id);
    simg_write_png(path, pixels, width, height, type);
    free(path);
}

void rip(const uint8_t *buffer, size_t size, int platform) {
    uint8_t *imghdr = simg_find_pit(buffer, size, platform);
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
                write_png(pixels, simg.width, simg.height, simg.type, i);
            } else {
                write_png(buffer + bitmap_offset, simg.width, simg.height, simg.type, i);
            }
            if (pixels) {
                free(pixels);
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

void put_file_name(char *dest, const char *path) {
    if (!dest || !path) {
        return;
    }
    const char *s = strrchr(path, '/');
    s = (s) ? s + 1 : path;
    const char *e = strrchr(s, '.');
    size_t len = (e) ? e - s : strlen(s);
    strncpy(dest, s, len);
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
        // {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    // ReSharper disable once CppDFALoopConditionNotUpdated
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
            // case 'v': verbose = 1; break;
            case 'h':
                printf("Usage: %s [options] <fullflash.bin>\n", argv[0]);
                return 0;
            default: return 1;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "Error: fullflash file required\n");
        return 1;
    }
    const char *path = argv[optind];

    size_t size;
    uint8_t *buffer = load_ff_file(path, &size);
    if (buffer) {
        put_file_name(OUT_DIR, path);
        if (create_dir(OUT_DIR)) {
            const int platform = detect_platform(buffer, size);
            if (platform == -1) {
                fprintf(stderr, "Error: could not detect platform!\n");
            } else {
                char platform_name[8];
                if (platform == 0) {
                    strcpy(platform_name, "SG");
                } else if (platform == 1) {
                    strcpy(platform_name, "NSG");
                } else {
                    strcpy(platform_name, "ELKA");
                }
                fprintf(stdout, "Detect %s platform\n", platform_name);
                rip(buffer, size, platform);
            }
        } else {
            fprintf(stderr, "Error: could not create directory %s\n", OUT_DIR);
        }
        free(buffer);
    } else {
        fprintf(stderr, "Error: could not load fullflash file\n");
    }


    return 0;
}