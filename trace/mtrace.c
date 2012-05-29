//#include "mtrace.h"
#include "qlist.h"
#include "trace.h"
#include "mtrace.h"
#include "osdep.h"

struct mtrace_dev {
	char name[32];
	uint8_t devid;
	int state;
	QLIST_HEAD (, mtrace_addr) addr_list;
	QLIST_ENTRY (mtrace_dev) dev_next;
};

struct mtrace_addr {
	struct mtrace_reg reg;
	struct mtrace_dev *dev;
	QLIST_ENTRY(mtrace_addr) addr_next;
	QLIST_ENTRY(mtrace_addr) dev_next;
};

static QLIST_HEAD(,mtrace_dev) devlist;
static struct mtrace_dev *devhash[256];
static unsigned int cur_devid = 0;

/* addr hash table mgmt. */
struct mtrace_hash_entry {
	QLIST_HEAD(, mtrace_addr) addr_list;
};
static struct mtrace_hash_entry addr_hash_tbl[1024];


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
	for (i=0; i<ARRAY_SIZE(addr_hash_tbl); i++)
		QLIST_INIT(&addr_hash_tbl[i].addr_list);

	mtrace_inited = 1;
}
static void mtrace_reset(void)
{
	/* TODO */
	/* for all devices: remove addr entries & callback */
	/* init devlist */
	/* init addr_hash_tbl */
}

static int addr_hash(uint32_t v)
{
	return (v >> 12) & 0x3ff;
}

static struct mtrace_hash_entry* addr_hash_get(uint32_t paddr)
{
	int hash;
	struct mtrace_hash_entry *hentry;

	hash = 	addr_hash(paddr);
	hentry = &addr_hash_tbl[hash];

	return hentry;
}

/* insert new addr into hash table */
static void addr_hash_insert(struct mtrace_addr *addr)
{
	struct mtrace_hash_entry *entry;
	entry = addr_hash_get(addr->reg.paddr);

	QLIST_INSERT_HEAD(&entry->addr_list, addr, addr_next);
}

static void insert_addr(struct mtrace_addr *addr)
{
	struct mtrace_dev *dev = addr->dev;

	addr_hash_insert(addr);
	QLIST_INSERT_HEAD(&dev->addr_list, addr, dev_next);
}

static struct mtrace_addr* find_addr(uint32_t paddr, uint32_t size)
{
	struct mtrace_hash_entry *entry = addr_hash_get(paddr);
	struct mtrace_addr *addr;

	if (QLIST_EMPTY(&entry->addr_list))
		return NULL;

	QLIST_FOREACH(addr, &entry->addr_list, addr_next) {
		if ((addr->reg.paddr + addr->reg.size) <= paddr)
			continue;
		if ((paddr+size) <= addr->reg.paddr)
			continue;
		return addr;
	}

	return NULL;
}

static uint8_t _mtrace_get_devid(const char *name)
{
	struct mtrace_dev *dev;
	
	QLIST_FOREACH(dev, &devlist, dev_next) {
		if (!strcmp(name, dev->name)) {
			return dev->devid;
		}
	}

	return (uint8_t)-1;
}

uint8_t mtrace_get_devid(const char *name)
{
	uint8_t ret;

	mtrace_lock();

	ret = _mtrace_get_devid(name);

	mtrace_unlock();

	return ret;
}

static struct mtrace_dev* getdev_byid(int id)
{
	return devhash[id];
}

static struct mtrace_dev* getdev_byname(const char *devname)
{
	uint8_t devid = mtrace_get_devid(devname);
	if (devid == (uint8_t)-1)
		return NULL;
	return getdev_byid(devid);
}

void mtrace_add_filter(int devid, struct mtrace_reg *reg)
{
	/* add to monitor table */
	struct mtrace_addr *addr;
	struct mtrace_dev *dev;

	mtrace_lock();
	
	dev = getdev_byid(devid);
	if (!dev) {
		trace_mtrace_add_filter("error: no device", reg->paddr, reg->size);
		return;	/* TODO */
	}

	addr = g_malloc0(sizeof(*addr));
	memcpy(&addr->reg, reg, sizeof(*reg));
	addr->dev = dev;
	insert_addr(addr);

	mtrace_unlock();

	trace_mtrace_add_filter(dev->name, reg->paddr, reg->size);
}

void mtrace_del_filter(int devid, uint32_t paddr, uint32_t size)
{
	struct mtrace_addr *addr;
	struct mtrace_dev *dev;

	dev = getdev_byid(devid);
	if (dev)
		trace_mtrace_del_filter(dev->name, paddr);

	mtrace_lock();

	addr = find_addr(paddr, 4);
	if (!addr) {
		goto err_del_filter;
	}
	
	if (devid != dev->devid || dev != addr->dev) {
		/* TODO */
		goto err_del_filter;
	}

	QLIST_REMOVE(addr, addr_next);
	QLIST_REMOVE(addr, dev_next);

	addr->reg.del_callback(&addr->reg);
	g_free(addr);

err_del_filter:
	/* TODO */
	mtrace_unlock();

}

void mtrace_del_all(int devid)
{
	/* TODO */
}

/* TODO: get cpu state information (pc, stack frame) */
static void mtrace_hook_access(uint32_t paddr, uint32_t size, int write)
{
    struct mtrace_addr *addr;

    mtrace_lock();
   
	if (paddr & 3) {
   		/* TODO: it's critical, check unaligned access */ 
    	mtrace_unlock();
		return;
	}
    addr = find_addr(paddr, size);
    if (addr && addr->dev->state) {
		/* XXX: too verbose, consider to remove this line */
		trace_mtrace_hook_access(addr->dev->name, paddr, size, write);
		addr->reg.hook_callback(&addr->reg, paddr, size);
    }

    mtrace_unlock();
}

void mtrace_hook_read(uint32_t paddr, uint32_t size)
{
	mtrace_hook_access(paddr, size, 0);
}

void mtrace_hook_write(uint32_t paddr, uint32_t size)
{
	mtrace_hook_access(paddr, size, 0);
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
	QLIST_INIT(&dev->addr_list);
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

