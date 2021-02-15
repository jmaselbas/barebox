/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPLETION_H
#define __LINUX_COMPLETION_H

/*
 * (C) Copyright 2021 Ahmad Fatoum
 *
 * Async wait-for-completion handler data structures.
 * This allows pollers to wait for main thread and vice versa
 * Requires poller_yield() support
 */

#include <poller.h>
#include <linux/bug.h>

struct completion {
	unsigned int done;
};

static inline void init_completion(struct completion *x)
{
	x->done = 0;
}

static inline void reinit_completion(struct completion *x)
{
	x->done = 0;
}

static inline int wait_for_completion_interruptible(struct completion *x)
{
	int ret;

	while (!x->done) {
		ret = poller_reschedule();
		if (ret)
			return ret;
	}

	return 0;
}

static inline bool completion_done(struct completion *x)
{
	return x->done;
}

static inline void complete(struct completion *x)
{
	x->done = 1;
}

static inline void __noreturn complete_and_exit(struct completion *x, int ret)
{
	BUG_ON(!in_poller());
	complete(x);
	for (;;)
		poller_yield();
}

#endif
