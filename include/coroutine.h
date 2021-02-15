/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Ahmad Fatoum, Pengutronix
 */

#ifndef __COROUTINE_H_
#define __COROUTINE_H_

#include <linux/stddef.h>

struct coroutine;

#define __coroutine void

#ifdef CONFIG_HAS_ARCH_SJLJ

struct coroutine *coroutine_alloc(__coroutine (*entry)(void *), void *arg);
void coroutine_schedule(struct coroutine *coro);
void coroutine_yield(struct coroutine *coro);
void coroutine_free(struct coroutine *coro);

#else

static inline struct coroutine *coroutine_alloc(__coroutine (*entry)(void *), void *arg)
{
	return NULL;
}

static inline void coroutine_schedule(struct coroutine *coro)
{
}

static inline void coroutine_yield(struct coroutine *coro)
{
}

static inline void coroutine_free(struct coroutine *coro)
{
}

#endif

#endif
