#include "fi.h"
#include "monitor.h"
#include "trace.h"

#define FI_TRIGGER_MASK     0x0000fff0
#define FI_TRIGGER_RATE     0x00000000
#define FI_TRIGGER_TIME     0x00000010
#define FI_TRIGGER_PC       0x00000030

#define FI_ENABLE_MASK      0x00000001

#define DEBUG_FI

#if defined(DEBUG_FI)
#define DPRINTF(fmt, ...) do { printf ("fi: " fmt, ##__VA_ARGS__); } while(0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

struct fi_trigger_desc;
struct fi_attr {
    uint32_t flags;
    struct fi_info *info;
    struct fi_trigger_desc *desc;
    QLIST_ENTRY(fi_attr) link;
    int (*should_fail)(struct fi_attr *attr);
    void (*show_status)(struct fi_attr *attr);
    /* stats */
    uint32_t ntrig; /* injected */
    uint32_t ntries;    /* # of calls to should_fail */
};

static QLIST_HEAD (,fi_info) fi_list = QLIST_HEAD_INITIALIZER();

/* fault trigger types */
struct fi_attr_rate{
    struct fi_attr attr;
    uint32_t rate;   /* 0: not triggered, n>0: triggered by nth time */
    uint32_t maxcnt;
};

struct fi_attr_time{
    struct fi_attr attr;
    /* TODO: not yet implemented */
};

struct fi_trigger_desc {
    uint32_t type;
    const char *name;
    const char *desc;
    struct fi_attr*(*constructor)(struct fi_info *fi, struct fi_trigger_desc *desc, const QDict *qdict);
};

static int fi_trigger_rate_should_fail(struct fi_attr *ptr)
{
    struct fi_attr_rate *attr = (struct fi_attr_rate*)ptr;
    int ret;
    
    if (attr->rate == 0 || (attr->maxcnt && ptr->ntrig > attr->maxcnt))
        ret = 0;
    else if (attr->rate == 1)
        ret = 1;
    else if ((ptr->ntries % attr->rate) == 0)
        ret = 1;
    else
        ret = 0;
    
    return ret;
}
static void fi_trigger_rate_show(struct fi_attr *attr)
{
}

static struct fi_attr* create_fi_attr(struct fi_info *fi,
                        struct fi_trigger_desc *desc,
                        size_t size,
                        int (*should_fail)(struct fi_attr *),
                        void (*show_status)(struct fi_attr *))
{
    struct fi_attr *attr;
    attr = g_malloc0(size);
    if (!attr)
        return NULL;
    memset(attr, 0, sizeof(attr));
    attr->desc = desc;
    attr->should_fail = should_fail;
    attr->show_status = show_status;
    return attr;
}

/* fault:s,trigger:s?,command:s?,opt1:s?,opt2:opt3 */
static struct fi_attr* trigger_rate_constructor(struct fi_info *fi,
                        struct fi_trigger_desc *desc, const QDict *qdict)
{
    struct fi_attr_rate *attr;
    const char *opt1= qdict_get_try_str(qdict, "opt1");
    const char *opt2= qdict_get_try_str(qdict, "opt2");

    attr = (struct fi_attr_rate *)create_fi_attr(fi, desc, sizeof(*attr),
                                    fi_trigger_rate_should_fail, fi_trigger_rate_show);
    if (!attr)
        return NULL;
    attr->rate = opt1 ? (uint32_t)strtoul(opt1, NULL, 10) : 0;
    attr->maxcnt = opt2 ? (uint32_t)strtoul(opt2, NULL, 10) : 0;

    DPRINTF ("rate trigger added: %u/%u\n", attr->rate, attr->maxcnt);
    DPRINTF ("attr=%p, %p\n", attr, &attr->attr);
    return &attr->attr;
}

struct fi_trigger_desc fi_trigger_desc[] = {
    { FI_TRIGGER_RATE, "rate", "...", trigger_rate_constructor, },
};

static void fi_lock(void)
{
}

static void fi_unlock(void)
{
}

static void fi_enable(struct fi_attr *attr)
{
    attr->flags &= ~(FI_ENABLE_MASK);
}
static void fi_disable(struct fi_attr *attr)
{
    attr->flags |= FI_ENABLE_MASK;
}

static void fi_init(struct fi_info *fi)
{
    memset(fi, 0, sizeof(*fi));
    QLIST_INIT(&fi->attr_list);
}
/* TODO ref mgmt */
struct fi_info* fi_create(const char *name, const char *desc, int reg)
{
    struct fi_info *fi = g_malloc0(sizeof(*fi));
    if (!fi)
        return NULL;
    fi_init(fi);
    fi->name = name;
    fi->desc = desc;
    if (reg) {
        if (fi_register(fi)) {
            g_free(fi);
            return NULL;
        }
    }
    return fi;
}

int fi_register(struct fi_info* fi)
{
    fi_lock();
    QLIST_INSERT_HEAD(&fi_list, fi, link);
    fi_unlock();

    return 0;
}

int fi_unregister(struct fi_info* fi)
{
    struct fi_attr *attr, *tmp;

    fi_lock();
    QLIST_FOREACH_SAFE(attr, &fi->attr_list, link, tmp) {
        QLIST_REMOVE(attr, link);
        g_free(attr);
    }
    QLIST_REMOVE(fi, link);
    fi_unlock();

    return 0;
}

int fi_should_fail(struct fi_info* fi)
{
    struct fi_attr *attr;
    int ret = 0;

    fi_lock();

    QLIST_FOREACH(attr, &fi->attr_list, link) {
        if (attr->should_fail(attr)) {
            ret = 1;
            attr->ntrig++;
        }
        attr->ntries++;
    }

    fi_unlock();

    return ret;
}

static int fi_cmd(struct fi_info *fi, const QDict *qdict)
{
    const char *trigger = qdict_get_try_str(qdict, "trigger");
    const char *cmd = qdict_get_try_str(qdict, "command");
    struct fi_attr *attr, *tmp;
    int i;

    if (!cmd) {
        QLIST_FOREACH(attr, &fi->attr_list, link) {
            if (attr->show_status) {
                attr->show_status(attr);
                return 0;
            }
        }
    } else if (!strcmp(cmd, "on")) {
        for (i = 0; i < ARRAY_SIZE(fi_trigger_desc); i++) {
            struct fi_trigger_desc *desc = &fi_trigger_desc[i];
            if (!strcmp(desc->name, trigger)) {
                struct fi_attr* attr = desc->constructor(fi, desc, qdict);
                if (!attr) {
                    break;
                } else {
                    DPRINTF ("add triger %p to fi %p, now fi has : ", attr, fi);
                    QLIST_INSERT_HEAD(&fi->attr_list, attr, link);
                    QLIST_FOREACH(attr, &fi->attr_list, link) {
                        DPRINTF ("%p ", attr);
                    }
                    DPRINTF ("\n");
                    return 0;
                }
            }
        }
    } else {
        QLIST_FOREACH_SAFE(attr, &fi->attr_list, link, tmp) {
            if (!strcmp(attr->desc->name, trigger)) {
                QLIST_REMOVE(attr, link);
                g_free(attr);
                return 0;
            }
        }
    }

    return -1;
}

/* fault:s,trigger:s?,command:s?,opt1:s?,opt2:opt3 */
void do_fi(Monitor *mon, const QDict *qdict)
{
    const char *fault = qdict_get_try_str(qdict, "fault");
    struct fi_info *fi;
    int found = 0;

    fi_lock();
    QLIST_FOREACH(fi, &fi_list, link) {
        if (!strcmp(fi->name, fault)) {
            found = 1;
            if (fi_cmd(fi, qdict)) {
                monitor_printf(mon, "failed to process.\n");
            }
            break;
        }
    }

    if (!found)
        monitor_printf(mon, "fault %s is not registered.\n", fault);

    fi_unlock();
}

