/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Ahmad Fatoum, Pengutronix
 *
 * ASAN bookkeeping based on Qemu coroutine-ucontext.c
 */

/* To avoid future issues; fortify doesn't like longjmp up the call stack */
#ifndef __NO_FORTIFY
#define __NO_FORTIFY
#endif

#include <common.h>
#include <coroutine.h>
#include <asm/setjmp.h>
#include <linux/overflow.h>

/** struct coroutine
 *
 * @entry	coroutine entry point
 * @arg		coroutine user-supplied argument
 * @jmp_buf	coroutine context when yielded
 * @stack	pointer to stack bottom (for stacks growing down)
 * @stack_size	size of stack in bytes
 * @stack_space actual stack for coroutines; empty for main thread
 */
struct coroutine {
	__coroutine (*entry)(void *);
	void *arg;
	jmp_buf jmp_buf;
	void *stack;
	size_t stack_size;
	u8 stack_space[] __aligned(16);
};

/** enum coroutine_action
 *
 * @COROUTINE_CHECKPOINT	placeholder, 0 reserved for regular setjmp return
 * @COROUTINE_BOOTSTRAP		initial yield in trampline
 * @COROUTINE_ENTER		switching from scheduler to coroutine
 * @COROUTINE_YIELD		switching from coroutine to scheduler
 * @COROUTINE_TERMINATE		final yield in trampoline
 */
enum coroutine_action {
	COROUTINE_CHECKPOINT = 0,
	COROUTINE_BOOTSTRAP,
	COROUTINE_ENTER,
	COROUTINE_YIELD,
	COROUTINE_TERMINATE
};

/** The main "thread". Execution returns here when a coroutine yields */
static struct coroutine scheduler;
/** Argument for trampoline as initjmp doesn't pass an argument */
static struct coroutine *new_coro;

static enum coroutine_action coroutine_switch(struct coroutine *from,
					      struct coroutine *to,
					      enum coroutine_action action);

static void __noreturn coroutine_trampoline(void)
{
	struct coroutine *coro = new_coro;

	coroutine_switch(coro, &scheduler, COROUTINE_BOOTSTRAP);

	coro->entry(coro->arg);

	longjmp(scheduler.jmp_buf, COROUTINE_TERMINATE);
}

struct coroutine *coroutine_alloc(__coroutine (*entry)(void *), void *arg)
{
	struct coroutine *coro;
	int ret;

	coro = malloc(struct_size(coro, stack_space, CONFIG_STACK_SIZE));
	if (!coro)
		return NULL;

	coro->stack = coro->stack_space;
	coro->stack_size = CONFIG_STACK_SIZE;
	coro->entry = entry;
	coro->arg = arg;

	/* set up coroutine context with the new stack */
	ret = initjmp(coro->jmp_buf, coroutine_trampoline,
		      coro->stack + CONFIG_STACK_SIZE);
	if (ret) {
		free(coro);
		return NULL;
	}

	/* jump to new context to finish initialization */
	new_coro = coro;
	coroutine_schedule(coro);
	new_coro = NULL;

	return coro;
}

void coroutine_schedule(struct coroutine *coro)
{
	coroutine_switch(&scheduler, coro, COROUTINE_ENTER);
}

void coroutine_yield(struct coroutine *coro)
{
	coroutine_switch(coro, &scheduler, COROUTINE_YIELD);
}

void coroutine_free(struct coroutine *coro)
{
	free(coro);
}

/*
 * When using ASAN, it needs to be told when we switch stacks.
 */
static void start_switch_fiber(void **fake_stack, struct coroutine *to);
static void finish_switch_fiber(void *fake_stack_save);

static enum coroutine_action coroutine_switch(struct coroutine *from, struct coroutine *to,
					      enum coroutine_action action)
{
	void *fake_stack_save = NULL;
	int ret;

	if (action == COROUTINE_BOOTSTRAP)
		finish_switch_fiber(NULL);

	ret = setjmp(from->jmp_buf);
	if (ret == 0) {
		start_switch_fiber(action == COROUTINE_TERMINATE ? NULL : &fake_stack_save, to);
		longjmp(to->jmp_buf, COROUTINE_YIELD);
	}

	finish_switch_fiber(fake_stack_save);

	return ret;
}

#ifdef CONFIG_ASAN

void __sanitizer_start_switch_fiber(void **fake_stack_save, const void *bottom, size_t size);
void __sanitizer_finish_switch_fiber(void *fake_stack_save, const void **bottom_old, size_t *size_old);

static void finish_switch_fiber(void *fake_stack_save)
{
    const void *bottom_old;
    size_t size_old;

    __sanitizer_finish_switch_fiber(fake_stack_save, &bottom_old, &size_old);

    if (!scheduler.stack) {
        scheduler.stack = (void *)bottom_old;
        scheduler.stack_size = size_old;
    }
}

static void start_switch_fiber(void **fake_stack_save,
                               struct coroutine *to)
{
	__sanitizer_start_switch_fiber(fake_stack_save, to->stack, to->stack_size);
}

#else

static void finish_switch_fiber(void *fake_stack_save)
{
}

static void start_switch_fiber(void **fake_stack_save,
                               struct coroutine *to)
{
}

#endif
