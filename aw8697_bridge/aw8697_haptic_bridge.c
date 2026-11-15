/*
 * aw8697_haptic_bridge v2 - vibration for TWRP/OrangeFox on chopin, with
 * adjustable strength.
 *
 * TWRP/OrangeFox vibrate() only knows two fixed sysfs paths:
 *     /sys/class/leds/vibrator/{duration,activate}
 *     /sys/class/timed_output/vibrator/enable
 * The chopin kernel (aw8697 driver, built in) exposes the same controls on the
 * I2C device instead:  /sys/bus/i2c/devices/9-005a/{duration,activate,gain,...}
 *
 * What this daemon does:
 *   1. finds the real aw8697 device dir (under /sys/bus/i2c/drivers/aw8697_haptic)
 *   2. mounts a tmpfs on /sys/class/leds, re-creates a symlink for every LED
 *      that was there (lcd-backlight, torch, ...) and adds a "vibrator" dir
 *      with plain files "duration" and "activate"
 *   3. watches vibrator/activate with inotify; when TWRP closes it after a write:
 *        - the value TWRP wrote to "duration" (the OrangeFox vibration slider,
 *          10..300) is converted to a STRENGTH and written to the driver
 *          attribute "gain" (RAM waveform amplitude)
 *        - a fixed pulse length is written to the real "duration"
 *        - 1 is written to the real "activate"
 *
 * Why: in RAM mode the aw8697 plays a fixed waveform, so the "duration" the slider
 * produces has no audible effect. The amplitude is what makes it feel stronger.
 *
 * Live tuning (no rebuild): put key=value pairs in /tmp/aw8697_bridge.conf, e.g.
 *     adb shell "echo 'attr=gain fmt=hex min=0x40 max=0xff pulse=60' > /tmp/aw8697_bridge.conf"
 * The file is re-read whenever it changes. Keys:
 *     attr=NAME   driver attribute used for strength (default gain, "none" = off)
 *     fmt=hex|dec how the number is written (default hex, matches sscanf "%x")
 *     min=N max=N attribute value at slider min / slider max
 *     smin=N smax=N slider range (default 10 / 300)
 *     pulse=MS    real duration written each time (default 60, 0 = pass slider)
 *     mode=N      write N to activate_mode every time (default: untouched)
 *     index=N     write N to index every time (default: untouched)
 *
 * If anything fails before the mount is complete, nothing is changed.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
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
#define DEF_CONF "/tmp/aw8697_bridge.conf"
#define TMPFS_MAGIC_NUM 0x01021994

struct cfg {
    char attr[64]; /* "" = strength mapping disabled */
    int hex;       /* 1: hex digits, 0: decimal */
    long vmin, vmax;
    long smin, smax;
    long pulse;
    long mode;  /* -1 = untouched */
    long index; /* -1 = untouched */
};

static struct cfg g_cfg;
static const char *g_log = DEF_LOG;
static const char *g_conf = DEF_CONF;
static time_t g_conf_mtime = 0;
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

/* write a string to an existing file (no create); O_TRUNC == fopen("w") */
static int write_str(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
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

/* write "name" attribute of the real device */
static int hw_write(const char *name, const char *val)
{
    char p[PATH_MAX];
    if (path_join(p, sizeof(p), g_hw, name))
        return -1;
    return write_str(p, val);
}

/* ------------------------------------------------------------------ config */

static void cfg_defaults(struct cfg *c)
{
    memset(c, 0, sizeof(*c));
    copy_str(c->attr, sizeof(c->attr), "gain");
    c->hex = 1;
    c->vmin = 0x40;
    c->vmax = 0xff;
    c->smin = 10;
    c->smax = 300;
    c->pulse = 60;
    c->mode = -1;
    c->index = -1;
}

static int valid_attr_name(const char *s)
{
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '_'))
            return 0;
    return 1;
}

/* apply one key=value token; returns 0 if recognised */
static int cfg_set(struct cfg *c, const char *key, const char *val)
{
    char *end = NULL;
    long n = strtol(val, &end, 0);
    int is_num = (end != val && *end == '\0');

    if (!strcmp(key, "attr")) {
        if (!strcmp(val, "none") || !strcmp(val, "off")) {
            c->attr[0] = '\0';
            return 0;
        }
        if (!valid_attr_name(val))
            return -1;
        return copy_str(c->attr, sizeof(c->attr), val);
    }
    if (!strcmp(key, "fmt")) {
        if (!strcmp(val, "hex"))
            c->hex = 1;
        else if (!strcmp(val, "dec"))
            c->hex = 0;
        else
            return -1;
        return 0;
    }
    if (!is_num)
        return -1;
    if (!strcmp(key, "min")) c->vmin = n;
    else if (!strcmp(key, "max")) c->vmax = n;
    else if (!strcmp(key, "smin")) c->smin = n;
    else if (!strcmp(key, "smax")) c->smax = n;
    else if (!strcmp(key, "pulse")) c->pulse = n;
    else if (!strcmp(key, "mode")) c->mode = n;
    else if (!strcmp(key, "index")) c->index = n;
    else
        return -1;
    return 0;
}

static void cfg_load_file(void)
{
    struct stat st;
    if (stat(g_conf, &st) != 0)
        return;
    if (st.st_mtime == g_conf_mtime)
        return;
    g_conf_mtime = st.st_mtime;

    char buf[1024];
    if (read_str(g_conf, buf, sizeof(buf)) != 0)
        return;

    struct cfg c;
    cfg_defaults(&c); /* file always describes the full config */
    int bad = 0;
    for (char *tok = strtok(buf, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        if (tok[0] == '#')
            break;
        char *eq = strchr(tok, '=');
        if (!eq) {
            bad++;
            continue;
        }
        *eq = '\0';
        if (cfg_set(&c, tok, eq + 1) != 0)
            bad++;
    }
    if (c.smax <= c.smin) {
        c.smin = 10;
        c.smax = 300;
        bad++;
    }
    g_cfg = c;
    g_logged_events = 0; /* show the first events with the new settings */
    logf_("config loaded: attr=%s fmt=%s min=%ld max=%ld smin=%ld smax=%ld pulse=%ld mode=%ld index=%ld%s",
          c.attr[0] ? c.attr : "none", c.hex ? "hex" : "dec", c.vmin, c.vmax, c.smin, c.smax,
          c.pulse, c.mode, c.index, bad ? " (some tokens ignored)" : "");
}

/* slider value -> attribute value (linear, clamped) */
static long map_strength(const struct cfg *c, long slider)
{
    if (slider < c->smin)
        slider = c->smin;
    if (slider > c->smax)
        slider = c->smax;
    long span_s = c->smax - c->smin;
    long span_v = c->vmax - c->vmin;
    return c->vmin + (span_v * (slider - c->smin) + (span_v >= 0 ? span_s / 2 : -(span_s / 2))) / span_s;
}

/* -------------------------------------------------------------- hw lookup */

static int find_hw_dir(void)
{
    const char *over = getenv("AW8697_HW_DIR");
    if (over && *over) {
        if (copy_str(g_hw, sizeof(g_hw), over))
            return -1;
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

static int prepare_leds(void)
{
    int no_mount = getenv("AW8697_NO_MOUNT") != NULL;
    struct statfs sfs;
    int already = 0;

    if (!no_mount && statfs(g_leds, &sfs) == 0 && (unsigned long)sfs.f_type == TMPFS_MAGIC_NUM)
        already = 1;

    if (!no_mount && !already) {
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

        if (mount("tmpfs", g_leds, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=0755") != 0) {
            logf_("mount tmpfs on %s failed: %s (nothing changed)", g_leds, strerror(errno));
            free(links);
            return -1;
        }

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
    /* a symlinked "activate" means the old aw8697_sysfs.sh overlay owns this dir */
    struct stat lst;
    if (lstat(p, &lst) == 0 && S_ISLNK(lst.st_mode))
        logf_("WARNING: %s is a symlink - the old aw8697-sysfs script overlay is active. "
              "Remove that service/script, otherwise strength control cannot work.", p);
    return 0;
}

/* ---------------------------------------------------------------- vibrate */

static void do_vibrate(void)
{
    char buf[64], p[PATH_MAX];
    long slider = 50;

    cfg_load_file();

    if (path_join(p, sizeof(p), g_vib, "duration") == 0 && read_str(p, buf, sizeof(buf)) == 0) {
        long v = strtol(buf, NULL, 10);
        if (v > 0)
            slider = v;
    }

    int e_attr = 0, r_attr = 0, e_dur = 0, r_dur = 0, e_act = 0, r_act = 0;
    long strength = -1;

    /* 1. strength */
    if (g_cfg.attr[0]) {
        strength = map_strength(&g_cfg, slider);
        char val[32];
        snprintf(val, sizeof(val), g_cfg.hex ? "%lx" : "%ld", strength < 0 ? 0 : strength);
        r_attr = hw_write(g_cfg.attr, val);
        e_attr = errno;
    }
    /* 2. optional fixed mode/index */
    if (g_cfg.mode >= 0) {
        char m[32];
        snprintf(m, sizeof(m), "%ld", g_cfg.mode);
        hw_write("activate_mode", m);
    }
    if (g_cfg.index >= 0) {
        char m[32];
        snprintf(m, sizeof(m), "%ld", g_cfg.index);
        hw_write("index", m);
    }
    /* 3. pulse length */
    long ms = g_cfg.pulse > 0 ? g_cfg.pulse : slider;
    if (ms < 10)
        ms = 10;
    if (ms > g_max_ms)
        ms = g_max_ms;
    char dv[32];
    snprintf(dv, sizeof(dv), "%ld", ms);
    r_dur = hw_write("duration", dv);
    e_dur = errno;
    /* 4. go */
    r_act = hw_write("activate", "1");
    e_act = errno;

    if (g_logged_events < 10 || r_attr || r_dur || r_act) {
        if (g_logged_events < 60) {
            logf_("slider=%ld -> %s=%ld(%s) pulse=%ldms | attr:%s duration:%s activate:%s", slider,
                  g_cfg.attr[0] ? g_cfg.attr : "-", strength, g_cfg.hex ? "hex" : "dec", ms,
                  g_cfg.attr[0] ? (r_attr ? strerror(e_attr) : "ok") : "n/a",
                  r_dur ? strerror(e_dur) : "ok", r_act ? strerror(e_act) : "ok");
        }
        g_logged_events++;
    }
}

int main(int argc, char **argv)
{
    const char *e;

    if ((e = getenv("AW8697_LOG")) && *e)
        g_log = e;
    if ((e = getenv("AW8697_CONF")) && *e)
        g_conf = e;
    if (copy_str(g_leds, sizeof(g_leds), (e = getenv("AW8697_LEDS_DIR")) && *e ? e : DEF_LEDS_DIR) ||
        path_join(g_vib, sizeof(g_vib), g_leds, "vibrator")) {
        fprintf(stderr, "aw8697_bridge: path too long\n");
        return 1;
    }

    cfg_defaults(&g_cfg);
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!strncmp(argv[i], "--", 2) && eq) {
            *eq = '\0';
            cfg_set(&g_cfg, argv[i] + 2, eq + 1);
        } else if (!strncmp(argv[i], "--max-ms=", 9)) {
            g_max_ms = atol(argv[i] + 9);
        }
    }
    if (g_max_ms < 10)
        g_max_ms = 1000;

    signal(SIGPIPE, SIG_IGN);
    logf_("starting (v2)");

    if (find_hw_dir() != 0) {
        logf_("aw8697 device dir not found - nothing to do");
        return 1;
    }
    logf_("hardware dir: %s", g_hw);

    char buf[64], p[PATH_MAX];
    const char *probe[] = {"gain", "activate_mode", "index", "duration"};
    for (size_t i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
        if (path_join(p, sizeof(p), g_hw, probe[i]) == 0 && read_str(p, buf, sizeof(buf)) == 0) {
            buf[strcspn(buf, "\r\n")] = '\0';
            logf_("initial %s = %s", probe[i], buf);
        }
    }

    cfg_load_file();
    logf_("strength map: attr=%s fmt=%s %ld..%ld for slider %ld..%ld, pulse=%ldms", g_cfg.attr[0] ? g_cfg.attr : "none",
          g_cfg.hex ? "hex" : "dec", g_cfg.vmin, g_cfg.vmax, g_cfg.smin, g_cfg.smax, g_cfg.pulse);

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
