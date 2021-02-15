/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2010 Marc Kleine-Budde <mkl@pengutronix.de>
 */

#ifndef POLLER_H
#define POLLER_H

#include <linux/list.h>

struct coroutine;

struct poller_struct {
	void (*func)(struct poller_struct *poller);
	int registered;
	struct list_head list;
	char *name;
	struct coroutine *coroutine;
};

extern struct poller_struct *active_poller;

int poller_register(struct poller_struct *poller, const char *name);
int poller_unregister(struct poller_struct *poller);

struct poller_async;

struct poller_async {
	struct poller_struct poller;
	void (*fn)(void *);
	void *ctx;
	uint64_t end;
	int active;
};

int poller_async_register(struct poller_async *pa, const char *name);
int poller_async_unregister(struct poller_async *pa);

int poller_call_async(struct poller_async *pa, uint64_t delay_ns,
		void (*fn)(void *), void *ctx);
int poller_async_cancel(struct poller_async *pa);
static inline bool poller_async_active(struct poller_async *pa)
{
	return pa->active;
}

static inline bool in_poller(void)
{
	return active_poller != NULL;
}

#ifdef CONFIG_POLLER
void poller_call(void);
#else
static inline void poller_call(void)
{
}
#endif /* CONFIG_POLLER */

/* Only for use when CONFIG_POLLER_YIELD=y */
void poller_yield(void);
int poller_reschedule(void);

#endif	/* !POLLER_H */
