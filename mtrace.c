#include "qemu-common.h"
#include "mtrace.h"
#include "qlist.h"
#include "trace.h"
#include "mtrace.h"
#include "osdep.h"
#include "qemu-thread.h"

#define MTRACE_DEBUG
#if defined(MTRACE_DEBUG)
#define DPRINTF(fmt, ...) do { printf("mtrace: " fmt,##__VA_ARGS__); } while(0)
#else
#define DPRINTF(fmt, ...)
#endif

struct mtrace_dev {
	char name[32];
	int state;
	QLIST_HEAD (, mtrace_reg) reg_list;
	QLIST_ENTRY (mtrace_dev) link;
	void* priv;
};

static QLIST_HEAD(,mtrace_dev) devlist;
static QemuMutex lock;

/* addr hash table mgmt. */
struct mtrace_hash_entry {
	QLIST_HEAD(, mtrace_reg) reg_list;
};
static struct mtrace_hash_entry reg_hash_tbl[1024];
static uint32_t reg_cnt = 0;

static void mtrace_lock(void)
{
    qemu_mutex_lock(&lock);
}

static void mtrace_unlock(void)
{
    qemu_mutex_unlock(&lock);
}

#if defined(MTRACE_DEBUG_REG)
static void debug_hook(struct mtrace_reg* reg, uintptr_t pc,
                uint32_t paddr, uint32_t size, int write, uint8_t *data)
{
	printf ("debug callback %08x write %d size %x\n", paddr, write, size);
}
#endif

static int mtrace_inited = 0;
void mtrace_init(void)
{
	int i;

    if (mtrace_inited)	
        return;
    
    qemu_mutex_init(&lock);

	reg_cnt = 0;
	QLIST_INIT(&devlist);
	
    for (i=0; i<ARRAY_SIZE(reg_hash_tbl); i++)
		QLIST_INIT(&reg_hash_tbl[i].reg_list);

    DPRINTF("inited\n");
	mtrace_inited = 1;
    
#if defined(MTRACE_DEBUG_REG)
    {
        void* dev = mtrace_register_dev("debug", 1, NULL);
        struct mtrace_reg *reg = NULL;
        reg = g_malloc(sizeof(*reg));
        memset(reg, 0, sizeof(*reg));
        reg->paddr = 0x80000000;
        reg->size = 16;
		reg->hook_callback = debug_hook;
        mtrace_add_filter(dev, reg);
    }
#endif
}

static struct mtrace_dev* getdev_byname(const char *devname)
{
    struct mtrace_dev *dev;

    if (!devname)
        return NULL;

    QLIST_FOREACH(dev, &devlist, link) {
        if (!strcmp(dev->name, devname))
            return dev;
    }
    return NULL;
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
static inline void reg_hash_insert(struct mtrace_reg *reg)
{
	struct mtrace_hash_entry *entry;
	entry = reg_hash_get(reg->paddr);

	QLIST_INSERT_HEAD(&entry->reg_list, reg, hashlink);
}

static inline void reg_remove(struct mtrace_reg *reg)
{
    QLIST_REMOVE(reg, hashlink);
    QLIST_REMOVE(reg, devlink);
    if (reg->del_callback)
        reg->del_callback(reg);
    else
        g_free(reg);
}

static inline void reg_insert(struct mtrace_reg *reg)
{
	struct mtrace_dev *dev = reg->dev;
    reg_hash_insert(reg);
    QLIST_INSERT_HEAD(&dev->reg_list, reg, devlink);
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
		if ((paddr + size) <= reg->paddr)
			continue;
		return reg;
	}

	return NULL;
}

void mtrace_add_filter(void *dev, struct mtrace_reg *reg)
{
	mtrace_lock();
    reg->dev = dev;
	reg_insert(reg);
	reg_cnt++;
    
	mtrace_unlock();
}

void mtrace_del_filter(struct mtrace_reg *reg)
{
	mtrace_lock();
    
    reg_remove(reg);
	if (reg_cnt == 0 ) {
		DPRINTF ("reg_cnt unbanlanced!!\n");
	}
	reg_cnt--;

	mtrace_unlock();
}

int mtrace_del_all(void *ptr)
{
	struct mtrace_dev *dev = ptr;
    struct mtrace_reg *tmp, *reg;
    int cnt = 0;

    mtrace_lock();

    if (!dev) {
        fprintf (stderr, "mtrace: wrong dev info %p.\n", dev);
        return cnt;
    }
    QLIST_FOREACH_SAFE(reg, &dev->reg_list, devlink, tmp) {
        cnt++;
        reg_remove(reg);
    }
   
	if (reg_cnt < cnt) {
		DPRINTF ("reg count unbalanced (del_all: %d/%d)!!\n", reg_cnt, cnt);
	}
   	reg_cnt -= cnt; 
    //DPRINTF ("---------- remove all filters\n");
    mtrace_unlock();
    return cnt;
}

/* TODO: get cpu state information (pc, stack frame) */
int mtrace_hook_access(void *e, uint32_t paddr, uint32_t size,
                                int write, void* data)
{
	CPUArchState *env = e;
    struct mtrace_reg *reg;
    struct mtrace_dev *dev;
    int ret = 0;
	uint32_t pc = cpu_get_pc(env);

  	if (reg_cnt < 1) {
		return 0;
	}
    
	mtrace_lock();

    /* XXX: performance critical part */
    reg = reg_find(paddr, size);

    if (unlikely(reg)) {
        dev = (struct mtrace_dev *)reg->dev;
        if (dev->state) {
        	//DPRINTF ("hook matched %x-%x %d!\n", paddr, size, write);
            if (likely(reg->hook_callback))
                ret = reg->hook_callback(reg, pc, paddr, size, write, data);
    		
			mtrace_unlock();
		
			if (ret) {
				env->exception_index = EXCP_DEBUG;
				cpu_loop_exit(env);
			}
            return 1;
        }
    } else {
        //DPRINTF ("hook %x-%x\n", paddr, size);
    }
	mtrace_unlock();

	return 0;
}

void* mtrace_dev_priv(void *dev)
{
	struct mtrace_dev *d = (struct mtrace_dev *)dev;
	return d->priv;
}
void* mtrace_register_dev(const char *name, int enable, void* priv)
{
	struct mtrace_dev *dev;

	if (!mtrace_inited) {
		mtrace_init();
	}
    
	mtrace_lock();

	dev = g_malloc0(sizeof(*dev));
	strncpy(dev->name, name, sizeof(dev->name));
	dev->state = enable;
	dev->priv = priv;
	QLIST_INIT(&dev->reg_list);
	QLIST_INSERT_HEAD(&devlist, dev, link);

    DPRINTF ("dev %s registered. state=%d\n", name, dev->state);
   	mtrace_unlock();

	return dev;
};

int mtrace_unregister_dev(const char *name)
{
    return 0;
}

static void mtrace_dev_control(const char *devname, int enable)
{
	struct mtrace_dev *dev;

	mtrace_lock();

	dev = getdev_byname(devname);

	if (dev) {
        if (enable == -1) {
            struct mtrace_reg *reg;
			printf ("Current %d entries registered.\n", reg_cnt);
	        QLIST_FOREACH(reg, &dev->reg_list, devlink) {
                printf ("\t0x%08x, len=%d\n", reg->paddr, reg->size);
            }
        } else {
		    dev->state = enable ? 1 : 0;
			DPRINTF ("dev %s set state = %d\n", dev->name, dev->state);
        }
	} else {
		/* TODO */
        fprintf (stderr, "dev %s is not registered.\n", devname);
	}
	
	mtrace_unlock();
}

int mtrace_control(const char *devname, int on)
{
	if (!mtrace_inited) {
		mtrace_init();
	}
	mtrace_dev_control(devname, on);
	return 0;
}

