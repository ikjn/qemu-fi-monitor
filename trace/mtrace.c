//#include "mtrace.h"
#include "qlist.h"
#include "trace.h"
#include "mtrace.h"
#include "osdep.h"

struct mtrace_dev {
	char name[32];
	uint8_t devid;
	int state;
	QLIST_HEAD (, mtrace_reg) reg_list;
	QLIST_ENTRY (mtrace_dev) dev_next;
};

static QLIST_HEAD(,mtrace_dev) devlist;
static struct mtrace_dev *devhash[256];
static unsigned int cur_devid = 0;

/* addr hash table mgmt. */
struct mtrace_hash_entry {
	QLIST_HEAD(, mtrace_reg) reg_list;
};
static struct mtrace_hash_entry reg_hash_tbl[1024];

static void mtrace_lock(void)
{
}

static void mtrace_unlock(void)
{
}

static int mtrace_inited = 0;
static void mtrace_init(void)
{
	int i;
	/* lock init */
	QLIST_INIT(&devlist);
	cur_devid = 0;
	memset(devhash, 0, sizeof(devhash));
	for (i=0; i<ARRAY_SIZE(reg_hash_tbl); i++)
		QLIST_INIT(&reg_hash_tbl[i].reg_list);

	mtrace_inited = 1;
}
static void mtrace_reset(void)
{
	/* TODO */
	/* for all devices: remove addr entries & callback */
	/* init devlist */
	/* init addr_hash_tbl */
}

static uint8_t getid_bydev(const char *name)
{
	struct mtrace_dev *dev;
	
	QLIST_FOREACH(dev, &devlist, dev_next) {
		if (!strcmp(name, dev->name)) {
			return dev->devid;
		}
	}

	return (uint8_t)-1;
}

static struct mtrace_dev* getdev_byid(int id)
{
	return devhash[id];
}

static struct mtrace_dev* getdev_byname(const char *devname)
{
	uint8_t devid = getid_bydev(devname);
	if (devid == (uint8_t)-1)
		return NULL;
	return getdev_byid(devid);
}

uint8_t mtrace_get_devid(const char *name)
{
	uint8_t ret;

	mtrace_lock();

	ret = getid_bydev(name);

	mtrace_unlock();

	return ret;
}

static int reg_hash_value(uint32_t v)
{
	return (v >> 12) & 0x3ff;
}

static struct mtrace_hash_entry* reg_hash_get(uint32_t paddr)
{
	int hash;
	struct mtrace_hash_entry *hentry;

	hash = 	reg_hash_value(paddr);
	hentry = &reg_hash_tbl[hash];

	return hentry;
}

/* insert new addr into hash table */
static void reg_hash_insert(struct mtrace_reg *reg)
{
	struct mtrace_hash_entry *entry;
	entry = reg_hash_get(reg->paddr);

	QLIST_INSERT_HEAD(&entry->reg_list, reg, hashlink);
}

static void reg_insert(struct mtrace_reg *reg)
{
	struct mtrace_dev *dev = getdev_byid(reg->devid);
    if (!dev) {
        /* XXX */
    } else {
        reg_hash_insert(reg);
        QLIST_INSERT_HEAD(&dev->reg_list, reg, hashlink);
    }
}

static struct mtrace_reg* reg_find(uint32_t paddr, uint32_t size)
{
	struct mtrace_hash_entry *entry = reg_hash_get(paddr);
	struct mtrace_reg *reg;

	if (QLIST_EMPTY(&entry->reg_list))
		return NULL;

	QLIST_FOREACH(reg, &entry->reg_list, hashlink) {
		if ((reg->paddr + reg->size) <= paddr)
			continue;
		if ((paddr+size) <= reg->paddr)
			continue;
		return reg;
	}

	return NULL;
}

void mtrace_add_filter(struct mtrace_reg *reg)
{
	/* add to monitor table */
	struct mtrace_dev *dev;

	mtrace_lock();
	
	dev = getdev_byid(reg->devid);
	if (!dev) {
		trace_mtrace_add_filter("error: no device", reg->paddr, reg->size);
		return;	/* TODO */
	}
    
    /* TODO: conflit check */
	reg_insert(reg);

	mtrace_unlock();

	trace_mtrace_add_filter(dev->name, reg->paddr, reg->size);
}

void mtrace_del_filter(struct mtrace_reg *reg)
{
	struct mtrace_dev *dev;
    struct mtrace_reg *tmp;

	mtrace_lock();

	dev = getdev_byid(reg->devid);

	if (dev)
		trace_mtrace_del_filter(dev->name, reg->paddr);
    else
		trace_mtrace_del_filter("error: no device", reg->paddr);

	tmp = reg_find(reg->paddr, reg->size);
	if (!tmp)
		goto err_del_filter;

	QLIST_REMOVE(reg, hashlink);
	QLIST_REMOVE(reg, devlink);

	reg->del_callback(reg);
	g_free(reg);

err_del_filter:
	mtrace_unlock();
}

void mtrace_del_all(int devid)
{
	/* TODO */
}

/* TODO: get cpu state information (pc, stack frame) */
static void mtrace_hook_access(uint32_t paddr, uint32_t size,
                                int write, uint8_t *data)
{
    struct mtrace_reg *reg;

    mtrace_lock();
   
    /* XXX: performance critical part */
    reg = reg_find(paddr, size);

    if (unlikely(reg)) {
        trace_mtrace_hook_access(reg->devid, paddr, size, write);
        if (!(paddr & 3)) {
            reg->hook_callback(reg, paddr, size, write, data);
        }
    } else {
        /* TODO */
        //trace_mtrace_hook_access(0, paddr, size, write);
    }

    mtrace_unlock();
}

void mtrace_hook_read(uint32_t paddr, uint32_t size)
{
	mtrace_hook_access(paddr, size, 0, NULL);
}

void mtrace_hook_write(uint32_t paddr, uint32_t size, uint8_t *data)
{
	mtrace_hook_access(paddr, size, 1, data);
}

int mtrace_register_dev(const char *name, int enable)
{
	struct mtrace_dev *dev;
	uint8_t devid;

	mtrace_lock();

	if (cur_devid < 256)
		devid = cur_devid++;
	else {
		/* TODO */
		mtrace_unlock();
		return -1;
	}

	dev = g_malloc0(sizeof(*dev));
	strncpy(dev->name, name, sizeof(dev->name));
	dev->state = enable;
	devhash[devid] = dev;
	QLIST_INIT(&dev->reg_list);
	QLIST_INSERT_HEAD(&devlist, dev, dev_next);

	mtrace_unlock();

	trace_mtrace_register_dev(name, enable);
	return dev->devid;
};

static void mtrace_dev_control(const char *devname, int enable)
{
	struct mtrace_dev *dev;

	mtrace_lock();

	dev = getdev_byname(devname);
	if (dev) {
		dev->state = enable;
	} else {
		/* TODO */
	}
	
	mtrace_unlock();
}

int mtrace_control(const char *devname, bool on)
{
	if (!mtrace_inited) {
		mtrace_init();
	}
	mtrace_dev_control(devname, on ? 1 : 0);
	return 0;
}

