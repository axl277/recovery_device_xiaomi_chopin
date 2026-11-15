/*
 * chopin_screencap - take a screenshot of the recovery screen from the command line.
 *
 * OrangeFox R12 on chopin has no working screenshot key combo. OrangeFox draws
 * through DRM ("Atomic Commit ... Using drm graphics", DRM_FORMAT_XBGR8888), so
 * this tool opens the DRM device as root, finds the framebuffer that is being
 * scanned out, maps it read-only and saves it as a BMP (or PPM).
 *
 *   adb shell chopin_screencap -o /tmp/shot.bmp
 *   adb pull /tmp/shot.bmp
 * or directly to the PC:
 *   adb exec-out chopin_screencap -o - > shot.bmp
 *
 * Options:
 *   -o FILE     output file ("-" = stdout), default /tmp/screenshot.bmp
 *   -d DEVICE   DRM node (default: tries /dev/dri/card0..3)
 *   --swap      red and blue are swapped in the picture -> use this
 *   --ppm       write PPM (P6) instead of BMP
 *   --selftest FILE   write a synthetic test picture (no DRM needed)
 *
 * It only reads. It never changes what is on the display.
 *
 * The DRM structures are declared here so the build does not depend on which
 * kernel headers the toolchain ships. On the host they are verified against the
 * real <drm/drm.h> with -DSC_VERIFY.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ---- minimal copies of the uapi structures (see include/uapi/drm/drm*.h) ---- */
struct sc_card_res {
    uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
    uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;
    uint32_t min_width, max_width, min_height, max_height;
};
struct sc_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh, flags, type;
    char name[32];
};
struct sc_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
    struct sc_modeinfo mode;
};
struct sc_fb_cmd {
    uint32_t fb_id, width, height, pitch, bpp, depth, handle;
};
struct sc_map_dumb {
    uint32_t handle, pad;
    uint64_t offset;
};
struct sc_plane_res {
    uint64_t plane_id_ptr;
    uint32_t count_planes;
};
struct sc_plane {
    uint32_t plane_id, crtc_id, fb_id, possible_crtcs, gamma_size, count_format_types;
    uint64_t format_type_ptr;
};
struct sc_client_cap {
    uint64_t capability, value;
};

#define SC_IOCTL_SET_CLIENT_CAP _IOW('d', 0x0d, struct sc_client_cap)
#define SC_IOCTL_GETRESOURCES _IOWR('d', 0xA0, struct sc_card_res)
#define SC_IOCTL_GETCRTC _IOWR('d', 0xA1, struct sc_crtc)
#define SC_IOCTL_GETFB _IOWR('d', 0xAD, struct sc_fb_cmd)
#define SC_IOCTL_MAP_DUMB _IOWR('d', 0xB3, struct sc_map_dumb)
#define SC_IOCTL_GETPLANERES _IOWR('d', 0xB5, struct sc_plane_res)
#define SC_IOCTL_GETPLANE _IOWR('d', 0xB6, struct sc_plane)
#define SC_CAP_UNIVERSAL_PLANES 2

#ifdef SC_VERIFY /* host-only: prove the copies match the real kernel headers */
#include <drm/drm.h>
#include <drm/drm_mode.h>
_Static_assert(sizeof(struct sc_card_res) == sizeof(struct drm_mode_card_res), "card_res");
_Static_assert(sizeof(struct sc_modeinfo) == sizeof(struct drm_mode_modeinfo), "modeinfo");
_Static_assert(sizeof(struct sc_crtc) == sizeof(struct drm_mode_crtc), "crtc");
_Static_assert(sizeof(struct sc_fb_cmd) == sizeof(struct drm_mode_fb_cmd), "fb_cmd");
_Static_assert(sizeof(struct sc_map_dumb) == sizeof(struct drm_mode_map_dumb), "map_dumb");
_Static_assert(sizeof(struct sc_plane_res) == sizeof(struct drm_mode_get_plane_res), "plane_res");
_Static_assert(sizeof(struct sc_plane) == sizeof(struct drm_mode_get_plane), "plane");
_Static_assert(sizeof(struct sc_client_cap) == sizeof(struct drm_set_client_cap), "client_cap");
_Static_assert(SC_IOCTL_SET_CLIENT_CAP == DRM_IOCTL_SET_CLIENT_CAP, "ioctl set_client_cap");
_Static_assert(SC_IOCTL_GETRESOURCES == DRM_IOCTL_MODE_GETRESOURCES, "ioctl getresources");
_Static_assert(SC_IOCTL_GETCRTC == DRM_IOCTL_MODE_GETCRTC, "ioctl getcrtc");
_Static_assert(SC_IOCTL_GETFB == DRM_IOCTL_MODE_GETFB, "ioctl getfb");
_Static_assert(SC_IOCTL_MAP_DUMB == DRM_IOCTL_MODE_MAP_DUMB, "ioctl map_dumb");
_Static_assert(SC_IOCTL_GETPLANERES == DRM_IOCTL_MODE_GETPLANERESOURCES, "ioctl getplaneres");
_Static_assert(SC_IOCTL_GETPLANE == DRM_IOCTL_MODE_GETPLANE, "ioctl getplane");
_Static_assert(SC_CAP_UNIVERSAL_PLANES == DRM_CLIENT_CAP_UNIVERSAL_PLANES, "cap");
#endif

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/*
 * Source pixels are 32-bit little-endian XBGR8888 (memory order R,G,B,X) as used by
 * OrangeFox; with swap they are XRGB8888 (memory order B,G,R,X).
 * Output: 24-bit BMP (bottom-up) or PPM P6.
 */
static int write_image(FILE *f, const uint8_t *src, uint32_t w, uint32_t h, uint32_t pitch, int swap, int ppm)
{
    uint8_t *row = malloc((size_t)w * 3 + 4);
    if (!row)
        return -1;
    uint32_t rowsz = (w * 3 + 3u) & ~3u;

    if (ppm) {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
    } else {
        uint8_t hdr[54];
        memset(hdr, 0, sizeof(hdr));
        hdr[0] = 'B';
        hdr[1] = 'M';
        put32(hdr + 2, 54 + rowsz * h);
        put32(hdr + 10, 54);
        put32(hdr + 14, 40);
        put32(hdr + 18, w);
        put32(hdr + 22, h); /* positive = bottom-up */
        hdr[26] = 1;
        hdr[28] = 24;
        put32(hdr + 34, rowsz * h);
        put32(hdr + 38, 2835);
        put32(hdr + 42, 2835);
        if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
            free(row);
            return -1;
        }
    }

    for (uint32_t i = 0; i < h; i++) {
        uint32_t y = ppm ? i : (h - 1 - i);
        const uint8_t *s = src + (size_t)y * pitch;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t b0 = s[x * 4 + 0], b1 = s[x * 4 + 1], b2 = s[x * 4 + 2];
            uint8_t r = swap ? b2 : b0, g = b1, b = swap ? b0 : b2;
            if (ppm) {
                row[x * 3 + 0] = r;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = b;
            } else {
                row[x * 3 + 0] = b;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = r;
            }
        }
        size_t out = ppm ? (size_t)w * 3 : rowsz;
        if (!ppm)
            memset(row + (size_t)w * 3, 0, rowsz - w * 3);
        if (fwrite(row, 1, out, f) != out) {
            free(row);
            return -1;
        }
    }
    free(row);
    return 0;
}

/* ------------------------------------------------------------------ DRM */

static int find_fb_id(int fd, uint32_t *fb_id_out)
{
    /* universal planes lets us see the primary plane too; errors are fine */
    struct sc_client_cap cap = {SC_CAP_UNIVERSAL_PLANES, 1};
    ioctl(fd, SC_IOCTL_SET_CLIENT_CAP, &cap);

    /* 1. legacy path: crtc -> fb (also valid after atomic commits) */
    struct sc_card_res res;
    memset(&res, 0, sizeof(res));
    if (ioctl(fd, SC_IOCTL_GETRESOURCES, &res) == 0 && res.count_crtcs > 0) {
        uint32_t n = res.count_crtcs;
        uint32_t *crtcs = calloc(n, sizeof(uint32_t));
        if (crtcs) {
            struct sc_card_res r2;
            memset(&r2, 0, sizeof(r2));
            r2.count_crtcs = n;
            r2.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
            if (ioctl(fd, SC_IOCTL_GETRESOURCES, &r2) == 0) {
                for (uint32_t i = 0; i < r2.count_crtcs && i < n; i++) {
                    struct sc_crtc c;
                    memset(&c, 0, sizeof(c));
                    c.crtc_id = crtcs[i];
                    if (ioctl(fd, SC_IOCTL_GETCRTC, &c) == 0 && c.fb_id) {
                        *fb_id_out = c.fb_id;
                        free(crtcs);
                        return 0;
                    }
                }
            }
            free(crtcs);
        }
    }

    /* 2. planes */
    struct sc_plane_res pr;
    memset(&pr, 0, sizeof(pr));
    if (ioctl(fd, SC_IOCTL_GETPLANERES, &pr) == 0 && pr.count_planes > 0) {
        uint32_t n = pr.count_planes;
        uint32_t *ids = calloc(n, sizeof(uint32_t));
        if (ids) {
            struct sc_plane_res p2;
            memset(&p2, 0, sizeof(p2));
            p2.count_planes = n;
            p2.plane_id_ptr = (uint64_t)(uintptr_t)ids;
            if (ioctl(fd, SC_IOCTL_GETPLANERES, &p2) == 0) {
                for (uint32_t i = 0; i < p2.count_planes && i < n; i++) {
                    struct sc_plane pl;
                    memset(&pl, 0, sizeof(pl));
                    pl.plane_id = ids[i];
                    if (ioctl(fd, SC_IOCTL_GETPLANE, &pl) == 0 && pl.fb_id) {
                        *fb_id_out = pl.fb_id;
                        free(ids);
                        return 0;
                    }
                }
            }
            free(ids);
        }
    }
    return -1;
}

static int capture(const char *dev, FILE *out, int swap, int ppm)
{
    int fd = open(dev, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "chopin_screencap: open %s: %s\n", dev, strerror(errno));
        return 2;
    }
    uint32_t fb_id = 0;
    if (find_fb_id(fd, &fb_id) != 0) {
        fprintf(stderr, "chopin_screencap: %s: no active framebuffer found\n", dev);
        close(fd);
        return 3;
    }
    struct sc_fb_cmd fb;
    memset(&fb, 0, sizeof(fb));
    fb.fb_id = fb_id;
    if (ioctl(fd, SC_IOCTL_GETFB, &fb) != 0) {
        fprintf(stderr, "chopin_screencap: GETFB(%u): %s\n", fb_id, strerror(errno));
        close(fd);
        return 4;
    }
    fprintf(stderr, "chopin_screencap: fb %u: %ux%u pitch=%u bpp=%u depth=%u handle=%u\n", fb.fb_id, fb.width,
            fb.height, fb.pitch, fb.bpp, fb.depth, fb.handle);
    if (fb.handle == 0) {
        fprintf(stderr, "chopin_screencap: kernel did not return a buffer handle (needs root)\n");
        close(fd);
        return 5;
    }
    if (fb.bpp != 32) {
        fprintf(stderr, "chopin_screencap: unsupported bpp %u (only 32 bpp)\n", fb.bpp);
        close(fd);
        return 6;
    }
    struct sc_map_dumb md;
    memset(&md, 0, sizeof(md));
    md.handle = fb.handle;
    if (ioctl(fd, SC_IOCTL_MAP_DUMB, &md) != 0) {
        fprintf(stderr, "chopin_screencap: MAP_DUMB: %s\n", strerror(errno));
        close(fd);
        return 7;
    }
    size_t len = (size_t)fb.pitch * fb.height;
    void *map = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, (off_t)md.offset);
    if (map == MAP_FAILED) {
        fprintf(stderr, "chopin_screencap: mmap: %s\n", strerror(errno));
        close(fd);
        return 8;
    }
    int rc = write_image(out, (const uint8_t *)map, fb.width, fb.height, fb.pitch, swap, ppm) == 0 ? 0 : 9;
    if (rc)
        fprintf(stderr, "chopin_screencap: write failed: %s\n", strerror(errno));
    munmap(map, len);
    close(fd);
    return rc;
}

int main(int argc, char **argv)
{
    const char *outp = "/tmp/screenshot.bmp";
    const char *dev = NULL;
    const char *selftest = NULL;
    int swap = 0, ppm = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            outp = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc)
            dev = argv[++i];
        else if (!strcmp(argv[i], "--swap"))
            swap = 1;
        else if (!strcmp(argv[i], "--ppm"))
            ppm = 1;
        else if (!strcmp(argv[i], "--selftest") && i + 1 < argc)
            selftest = argv[++i];
        else {
            fprintf(stderr, "usage: %s [-o FILE|-] [-d /dev/dri/cardN] [--swap] [--ppm] [--selftest FILE]\n", argv[0]);
            return 1;
        }
    }

    if (selftest) {
        /* 64x48 test picture in XBGR8888: R=x*4, G=y*5, B=128 */
        enum { W = 64, H = 48 };
        uint8_t *buf = calloc(W * H, 4);
        if (!buf)
            return 1;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t *p = buf + ((size_t)y * W + x) * 4;
                p[0] = (uint8_t)(x * 4);
                p[1] = (uint8_t)(y * 5);
                p[2] = 128;
                p[3] = 0xff;
            }
        FILE *f = fopen(selftest, "wb");
        if (!f)
            return 1;
        int rc = write_image(f, buf, W, H, W * 4, swap, ppm);
        fclose(f);
        free(buf);
        return rc ? 1 : 0;
    }

    FILE *out = !strcmp(outp, "-") ? stdout : fopen(outp, "wb");
    if (!out) {
        fprintf(stderr, "chopin_screencap: cannot write %s: %s\n", outp, strerror(errno));
        return 1;
    }

    int rc = 2;
    if (dev) {
        rc = capture(dev, out, swap, ppm);
    } else {
        for (int n = 0; n < 4 && rc != 0; n++) {
            char d[32];
            snprintf(d, sizeof(d), "/dev/dri/card%d", n);
            if (access(d, F_OK) != 0)
                continue;
            rc = capture(d, out, swap, ppm);
        }
    }
    if (out != stdout)
        fclose(out);
    if (rc == 0 && out != stdout)
        fprintf(stderr, "chopin_screencap: saved %s\n", outp);
    if (rc != 0 && out != stdout)
        unlink(outp);
    return rc;
}
