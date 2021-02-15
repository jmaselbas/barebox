// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Marc Kleine-Budde <mkl@pengutronix.de>
 */

#include <common.h>
#include <driver.h>
#include <malloc.h>
#include <module.h>
#include <param.h>
#include <poller.h>
#include <clock.h>
#include <work.h>
#include <slice.h>
#include <coroutine.h>

static LIST_HEAD(poller_list);
struct poller_struct *active_poller;

static __coroutine poller_thread(void *data);

int poller_register(struct poller_struct *poller, const char *name)
{
	if (poller->registered)
		return -EBUSY;

	poller->name = xstrdup(name);

	if (IS_ENABLED(CONFIG_POLLER_YIELD))
		poller->coroutine = coroutine_alloc(poller_thread, poller);

	list_add_tail(&poller->list, &poller_list);
	poller->registered = 1;

	return 0;
}

int poller_unregister(struct poller_struct *poller)
{
	if (!poller->registered)
		return -ENODEV;


	list_del(&poller->list);
	poller->registered = 0;
	coroutine_free(poller->coroutine);
	free(poller->name);

	return 0;
}

static void poller_async_callback(struct poller_struct *poller)
{
	struct poller_async *pa = container_of(poller, struct poller_async, poller);

	if (!pa->active)
		return;

	if (get_time_ns() < pa->end)
		return;

	pa->active = 0;
	pa->fn(pa->ctx);
}

/*
 * Cancel an outstanding asynchronous function call
 *
 * @pa		the poller that has been scheduled
 *
 * Cancel an outstanding function call. Returns 0 if the call
 * has actually been cancelled or -ENODEV when the call wasn't
 * scheduled.
 *
 */
int poller_async_cancel(struct poller_async *pa)
{
	pa->active = 0;

	return 0;
}

/*
 * Call a function asynchronously
 *
 * @pa		the poller to be used
 * @delay	The delay in nanoseconds
 * @fn		The function to call
 *
 * This calls the passed function after a delay of delay_ns. Returns
 * a pointer which can be used as a cookie to cancel a scheduled call.
 */
int poller_call_async(struct poller_async *pa, uint64_t delay_ns,
		void (*fn)(void *), void *ctx)
{
	pa->ctx = ctx;
	pa->end = get_time_ns() + delay_ns;
	pa->fn = fn;
	pa->active = 1;

	return 0;
}

int poller_async_register(struct poller_async *pa, const char *name)
{
	pa->poller.func = poller_async_callback;
	pa->active = 0;

	return poller_register(&pa->poller, name);
}

int poller_async_unregister(struct poller_async *pa)
{
	return poller_unregister(&pa->poller);
}

static void __poller_yield(struct poller_struct *poller)
{
	if (WARN_ON(!poller))
		return;

	coroutine_yield(poller->coroutine);
}

#ifdef CONFIG_POLLER_YIELD
/* No stub for this function. That way we catch wrong Kconfig dependencies
 * that enable code that uses poller_yield() unconditionally
 */
void poller_yield(void)
{
	return __poller_yield(active_poller);
}
#endif

int poller_reschedule(void)
{
	if (!in_poller())
		return ctrlc() ? -ERESTARTSYS : 0;

	__poller_yield(active_poller);
	return 0;
}

static __coroutine poller_thread(void *data)
{
	struct poller_struct *poller = data;

	for (;;) {
		poller->func(poller);
		__poller_yield(poller);
	}
}

static void poller_schedule(struct poller_struct *poller)
{
	if (!IS_ENABLED(CONFIG_POLLER_YIELD)) {
		poller->func(poller);
		return;
	}

	coroutine_schedule(poller->coroutine);
}

void poller_call(void)
{
	struct poller_struct *poller, *tmp;
	bool run_workqueues = !slice_acquired(&command_slice);

	if (active_poller)
		return;

	command_slice_acquire();

	if (run_workqueues)
		wq_do_all_works();

	list_for_each_entry_safe(poller, tmp, &poller_list, list) {
		active_poller = poller;
		poller_schedule(poller);
	}

	active_poller = NULL;
	command_slice_release();
}

#if defined CONFIG_CMD_POLLER

#include <command.h>
#include <getopt.h>
#include <clock.h>

static int poller_time(void)
{
	uint64_t start = get_time_ns();
	int i = 0;

	/*
	 * How many times we can run the registered pollers in one second?
	 *
	 * A low number here may point to problems with pollers taking too
	 * much time.
	 */
	while (!is_timeout(start, SECOND))
		i++;

	return i;
}

static void poller_info(void)
{
	struct poller_struct *poller;

	printf("Registered pollers:\n");

	if (list_empty(&poller_list)) {
		printf("<none>\n");
		return;
	}

	list_for_each_entry(poller, &poller_list, list)
		printf("%s\n", poller->name);
}

BAREBOX_CMD_HELP_START(poller)
BAREBOX_CMD_HELP_TEXT("print info about registered pollers")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_TEXT("Options:")
BAREBOX_CMD_HELP_OPT ("-i", "Print information about registered pollers")
BAREBOX_CMD_HELP_OPT ("-t", "measure how many pollers we run in 1s")
BAREBOX_CMD_HELP_OPT ("-c", "run coroutine test")
BAREBOX_CMD_HELP_END

static void poller_coroutine(struct poller_struct *poller)
{
	volatile u64 start;
	volatile int i = 0;

	for (;;) {
		start = get_time_ns();
		while (!is_timeout_non_interruptible(start, 225 * MSECOND))
			__poller_yield(active_poller);

		printf("Poller yield #%d\n", ++i);
	}
}

static int do_poller(int argc, char *argv[])
{
	struct poller_struct poller = {};
	int ret, opt;

	while ((opt = getopt(argc, argv, "itc")) > 0) {
		switch (opt) {
		case 'i':
			poller_info();
			return 0;
		case 'c':
			if (!IS_ENABLED(CONFIG_POLLER_YIELD)) {
				printf("CONFIG_POLLER_YIELD support not compiled in\n");
				return -ENOSYS;
			}

			poller.func = poller_coroutine;
			ret = poller_register(&poller, "poller-coroutine-test");
			if (ret)
				return ret;

			/* fallthrough */
		case 't':
			printf("%d poller calls in 1s\n", poller_time());
			return poller.func ? poller_unregister(&poller) : 0;
		}
	}

	return COMMAND_ERROR_USAGE;
}

BAREBOX_CMD_START(poller)
	.cmd = do_poller,
	BAREBOX_CMD_DESC("print info about registered pollers")
	BAREBOX_CMD_GROUP(CMD_GRP_MISC)
	BAREBOX_CMD_HELP(cmd_poller_help)
BAREBOX_CMD_END
#endif
