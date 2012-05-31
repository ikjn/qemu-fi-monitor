#ifndef _MTRACE_H_
#define _MTRACE_H_
#include "qlist.h"

struct mtrace_reg {
    uint32_t paddr;	/* physical address */
    uint32_t size;	/* range */
    uint8_t devid;
    void *opaque;
    /* XXX: move callbak ptr to dev structure */
	void (*hook_callback)(struct mtrace_reg* reg,
                uint32_t paddr, uint32_t size, int write, uint8_t *data);
	void (*del_callback)(struct mtrace_reg* reg);
    QLIST_ENTRY(mtrace_reg) hashlink;
    QLIST_ENTRY(mtrace_reg) devlink;
};

#if defined(CONFIG_TRACE_MEMORY)

uint8_t mtrace_get_devid(const char *name);
void mtrace_add_filter(struct mtrace_reg *reg);
void mtrace_del_filter(struct mtrace_reg *reg);
void mtrace_del_all(int devid);

/* called from cpu or other bus devices */
void mtrace_hook_read(uint32_t paddr, uint32_t size);
void mtrace_hook_write(uint32_t paddr, uint32_t size, uint8_t *data);

int mtrace_register_dev(const char *name, int enable);
int mtrace_control(const char *devname, bool on);

#else
/* TODO: define null funtions */
#endif

#endif

