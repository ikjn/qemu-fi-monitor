#ifndef _FI_H_
#define _FI_H_

#include "qlist.h"
#include "qdict.h"

struct fi_attr;
struct fi_info {
	const char *name;
	const char *desc;
	QLIST_HEAD(,fi_attr) attr_list;
    QLIST_ENTRY(fi_info) link;
};

struct fi_info* fi_create(const char *name, const char *desc, int reg);
int fi_register(struct fi_info* fi);
int fi_unregister(struct fi_info* fi);
void do_fi(Monitor *mon, const QDict *qdict);
int fi_should_fail(struct fi_info* fi);

#endif
