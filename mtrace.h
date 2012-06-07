#ifndef _MTRACE_H_
#define _MTRACE_H_
#include "qlist.h"

struct mtrace_reg {
    uint32_t paddr;	/* physical address */
    uint32_t size;	/* range */
    void *dev;
    void *opaque;
    /* XXX: move callbak ptr to dev structure */
	void (*hook_callback)(struct mtrace_reg* reg, uintptr_t pc,
                uint32_t paddr, uint32_t size, int write, uint8_t *data);
	void (*del_callback)(struct mtrace_reg* reg);
    QLIST_ENTRY(mtrace_reg) hashlink;
    QLIST_ENTRY(mtrace_reg) devlink;
};

void mtrace_init(void);
void mtrace_add_filter(void *dev, struct mtrace_reg *reg);
void mtrace_del_filter(struct mtrace_reg *reg);
int mtrace_del_all(void *dev);

/* called from cpu or other bus devices */
int mtrace_hook_read(uintptr_t pc, uint32_t paddr, uint32_t size);
int mtrace_hook_write(uintptr_t pc, uint32_t paddr, uint32_t size, uint8_t *data);

void* mtrace_register_dev(const char *name, int enable, void *priv);
int mtrace_unregister_dev(const char *name);
void* mtrace_dev_priv(void *dev);
int mtrace_control(const char *devname, int on);

#endif

