/*
 * aw8697_haptic_bridge - make TWRP/OrangeFox vibration work on chopin.
 *
 * TWRP/OrangeFox vibrate() only knows two fixed sysfs paths:
 *     /sys/class/leds/vibrator/{duration,activate}
 *     /sys/class/timed_output/vibrator/enable
 * The chopin kernel (aw8697 driver, built in) exposes the very same controls,
 * but on the I2C device:  /sys/bus/i2c/devices/9-005a/{duration,activate,...}
 * Neither of TWRP's paths exists, so nothing ever vibrates.
 *
 * What this daemon does:
 *   1. finds the real aw8697 device dir (under /sys/bus/i2c/drivers/aw8697_haptic)
 *   2. mounts a tmpfs on /sys/class/leds, re-creates a symlink for every LED
 *      that was there (lcd-backlight, torch, ...) and adds a "vibrator" dir
 *      with plain files "duration" and "activate"
 *   3. watches vibrator/activate with inotify; when TWRP closes it after a
 *      write, it copies duration -> real duration and writes 1 -> real activate
 *
 * If anything fails before the mount is complete, nothing is changed.
 *
 * Options:  --mode=N     write N to the real "activate_mode" once at start
 *           --max-ms=N   clamp the vibration length (default 1000)
 * Env (for testing off-device): AW8697_LEDS_DIR, AW8697_HW_DIR, AW8697_LOG,
 *                               AW8697_NO_MOUNT=1
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#define DEF_LEDS_DIR "/sys/class/leds"
#define DEF_DRV_DIR "/sys/bus/i2c/drivers/aw8697_haptic"
#define DEF_FALLBACK_HW "/sys/bus/i2c/devices/9-005a"
#define DEF_LOG "/tmp/aw8697_bridge.log"
#define TMPFS_MAGIC_NUM 0x01021994

static const char *g_log = DEF_LOG;
static char g_leds[PATH_MAX];
static char g_vib[PATH_MAX];
static char g_hw[PATH_MAX];
static long g_max_ms = 1000;
static int g_logged_events = 0;

static void logf_(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    FILE *f = fopen(g_log, "a");
    if (f) {
        time_t t = time(NULL);
        fprintf(f, "[%ld] %s\n", (long)t, msg);
        fclose(f);
    }
    fprintf(stderr, "aw8697_bridge: %s\n", msg);
}

/* write a string to an existing file (no create) */
static int write_str(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC); /* same as fopen("w") */
    if (fd < 0)
        return -1;
    size_t len = strlen(s);
    ssize_t w = write(fd, s, len);
    int saved = errno;
    close(fd);
    if (w != (ssize_t)len) {
        errno = saved;
        return -1;
    }
    return 0;
}

static int read_str(const char *path, char *buf, size_t sz)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t r = read(fd, buf, sz - 1);
    close(fd);
    if (r < 0)
        return -1;
    buf[r] = '\0';
    return 0;
}


/* dst = a + "/" + b ; returns -1 if it does not fit */
static int path_join(char *dst, size_t sz, const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la + 1 + lb + 1 > sz)
        return -1;
    memcpy(dst, a, la);
    dst[la] = '/';
    memcpy(dst + la + 1, b, lb + 1);
    return 0;
}

static int copy_str(char *dst, size_t sz, const char *src)
{
    size_t l = strlen(src);
    if (l + 1 > sz)
        return -1;
    memcpy(dst, src, l + 1);
    return 0;
}

static int path_exists(const char *p)
{
    return access(p, F_OK) == 0;
}

/* locate the real aw8697 device directory (the one that owns "activate") */
static int find_hw_dir(void)
{
    const char *over = getenv("AW8697_HW_DIR");
    if (over && *over) {
        if (copy_str(g_hw, sizeof(g_hw), over)) return -1;
        return path_exists(g_hw) ? 0 : -1;
    }

    for (int attempt = 0; attempt < 60; attempt++) {
        DIR *d = opendir(DEF_DRV_DIR);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (e->d_name[0] == '.' || !strcmp(e->d_name, "bind") ||
                    !strcmp(e->d_name, "unbind") || !strcmp(e->d_name, "uevent") ||
                    !strcmp(e->d_name, "module"))
                    continue;
                char dev[PATH_MAX], p[PATH_MAX];
                if (path_join(dev, sizeof(dev), DEF_DRV_DIR, e->d_name) ||
                    path_join(p, sizeof(p), dev, "activate"))
                    continue;
                if (path_exists(p)) {
                    copy_str(g_hw, sizeof(g_hw), dev);
                    closedir(d);
                    return 0;
                }
            }
            closedir(d);
        }
        sleep(1);
    }
    /* last resort: the path seen on chopin */
    char p[PATH_MAX];
    if (path_join(p, sizeof(p), DEF_FALLBACK_HW, "activate") == 0 && path_exists(p)) {
        copy_str(g_hw, sizeof(g_hw), DEF_FALLBACK_HW);
        return 0;
    }
    return -1;
}

struct led_link {
    char name[NAME_MAX + 1];
    char target[PATH_MAX];
};

static int create_plain_file(const char *path, const char *init)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0)
        return -1;
    fchmod(fd, 0666);
    if (init && write(fd, init, strlen(init)) < 0) {
        /* not fatal */
    }
    close(fd);
    return 0;
}

/* Build the fake /sys/class/leds. Returns 0 on success. */
static int prepare_leds(void)
{
    int no_mount = getenv("AW8697_NO_MOUNT") != NULL;
    struct statfs sfs;
    int already = 0;

    if (!no_mount && statfs(g_leds, &sfs) == 0 && (unsigned long)sfs.f_type == TMPFS_MAGIC_NUM)
        already = 1; /* we are being restarted, tmpfs is already there */

    if (!no_mount && !already) {
        /* 1. snapshot every existing LED with its resolved absolute target */
        struct led_link *links = NULL;
        size_t n = 0, cap = 0;
        DIR *d = opendir(g_leds);
        if (!d) {
            logf_("cannot open %s: %s", g_leds, strerror(errno));
            return -1;
        }
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.')
                continue;
            char src[PATH_MAX], real[PATH_MAX];
            if (path_join(src, sizeof(src), g_leds, e->d_name))
                continue;
            if (!realpath(src, real)) {
                logf_("realpath(%s) failed: %s (skipped)", src, strerror(errno));
                continue;
            }
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                struct led_link *nl = realloc(links, cap * sizeof(*links));
                if (!nl) {
                    free(links);
                    closedir(d);
                    return -1;
                }
                links = nl;
            }
            copy_str(links[n].name, sizeof(links[n].name), e->d_name);
            copy_str(links[n].target, sizeof(links[n].target), real);
            n++;
        }
        closedir(d);

        /* 2. mount tmpfs over it */
        if (mount("tmpfs", g_leds, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=0755") != 0) {
            logf_("mount tmpfs on %s failed: %s (nothing changed)", g_leds, strerror(errno));
            free(links);
            return -1;
        }

        /* 3. restore every LED as a symlink to the real device. Roll back on failure */
        for (size_t i = 0; i < n; i++) {
            char dst[PATH_MAX];
            if (path_join(dst, sizeof(dst), g_leds, links[i].name) ||
                (symlink(links[i].target, dst) != 0 && errno != EEXIST)) {
                logf_("symlink %s -> %s failed: %s; rolling back", dst, links[i].target, strerror(errno));
                umount2(g_leds, MNT_DETACH);
                free(links);
                return -1;
            }
        }
        logf_("tmpfs mounted on %s, %zu LED link(s) restored", g_leds, n);
        free(links);
    }

    /* 4. the fake vibrator node */
    if (mkdir(g_vib, 0755) != 0 && errno != EEXIST) {
        logf_("mkdir %s failed: %s", g_vib, strerror(errno));
        if (!no_mount && !already)
            umount2(g_leds, MNT_DETACH);
        return -1;
    }
    char p[PATH_MAX];
    if (path_join(p, sizeof(p), g_vib, "duration"))
        return -1;
    if (!path_exists(p) && create_plain_file(p, "0") != 0) {
        logf_("cannot create %s: %s", p, strerror(errno));
        return -1;
    }
    if (path_join(p, sizeof(p), g_vib, "activate"))
        return -1;
    if (!path_exists(p) && create_plain_file(p, "0") != 0) {
        logf_("cannot create %s: %s", p, strerror(errno));
        return -1;
    }
    return 0;
}

static void do_vibrate(void)
{
    char buf[64], p[PATH_MAX];
    long ms = 50;

    if (path_join(p, sizeof(p), g_vib, "duration") == 0 &&
        read_str(p, buf, sizeof(buf)) == 0) {
        long v = strtol(buf, NULL, 10);
        if (v > 0)
            ms = v;
    }
    if (ms < 10)
        ms = 10;
    if (ms > g_max_ms)
        ms = g_max_ms;

    char val[32];
    snprintf(val, sizeof(val), "%ld", ms);

    char hd[PATH_MAX], ha[PATH_MAX];
    if (path_join(hd, sizeof(hd), g_hw, "duration") || path_join(ha, sizeof(ha), g_hw, "activate"))
        return;

    int r1 = write_str(hd, val);
    int e1 = errno;
    int r2 = write_str(ha, "1");
    int e2 = errno;

    if (g_logged_events < 10 || r1 || r2) {
        if (g_logged_events < 40) {
            logf_("vibrate %ld ms -> duration:%s activate:%s", ms,
                  r1 ? strerror(e1) : "ok", r2 ? strerror(e2) : "ok");
        }
        g_logged_events++;
    }
}

int main(int argc, char **argv)
{
    const char *e;

    if ((e = getenv("AW8697_LOG")) && *e)
        g_log = e;
    if (copy_str(g_leds, sizeof(g_leds), (e = getenv("AW8697_LEDS_DIR")) && *e ? e : DEF_LEDS_DIR) ||
        path_join(g_vib, sizeof(g_vib), g_leds, "vibrator")) {
        fprintf(stderr, "aw8697_bridge: path too long\n");
        return 1;
    }

    int mode = -1;
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--mode=", 7))
            mode = atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "--max-ms=", 9))
            g_max_ms = atol(argv[i] + 9);
    }
    if (g_max_ms < 10)
        g_max_ms = 1000;

    signal(SIGPIPE, SIG_IGN);
    logf_("starting");

    if (find_hw_dir() != 0) {
        logf_("aw8697 device dir not found - nothing to do");
        return 1;
    }
    logf_("hardware dir: %s", g_hw);

    char buf[64], p[PATH_MAX];
    if (path_join(p, sizeof(p), g_hw, "activate_mode")) {
        logf_("path too long");
        return 1;
    }
    if (read_str(p, buf, sizeof(buf)) == 0) {
        buf[strcspn(buf, "\r\n")] = '\0';
        logf_("activate_mode currently: %s", buf);
    }
    if (mode >= 0) {
        char m[16];
        snprintf(m, sizeof(m), "%d", mode);
        if (write_str(p, m) == 0)
            logf_("activate_mode set to %d", mode);
        else
            logf_("cannot set activate_mode: %s", strerror(errno));
    }

    if (prepare_leds() != 0) {
        logf_("could not prepare %s - exiting (nothing changed)", g_leds);
        return 1;
    }

    int ifd = inotify_init1(IN_CLOEXEC);
    if (ifd < 0) {
        logf_("inotify_init1 failed: %s", strerror(errno));
        return 1;
    }
    if (inotify_add_watch(ifd, g_vib, IN_CLOSE_WRITE) < 0) {
        logf_("inotify_add_watch(%s) failed: %s", g_vib, strerror(errno));
        return 1;
    }
    logf_("ready, watching %s", g_vib);

    char evbuf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        ssize_t n = read(ifd, evbuf, sizeof(evbuf));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            logf_("inotify read failed: %s", strerror(errno));
            return 1;
        }
        for (char *q = evbuf; q < evbuf + n;) {
            struct inotify_event *ev = (struct inotify_event *)q;
            if ((ev->mask & IN_CLOSE_WRITE) && ev->len && !strcmp(ev->name, "activate"))
                do_vibrate();
            q += sizeof(struct inotify_event) + ev->len;
        }
    }
    return 0;
}
