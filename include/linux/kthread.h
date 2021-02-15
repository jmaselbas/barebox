/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
/* Wrapper around pollers to ease porting from Linux */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/string.h>
#include <poller.h>

struct task_struct {
	struct poller_struct poller;
	int (*threadfn)(void *data);
	void *data;
};

static inline void kthread_poller(struct poller_struct *poller)
{
	struct task_struct *task = container_of(poller, struct task_struct, poller);
	task->threadfn(task->data);
}

static inline struct task_struct *kthread_create(int (*threadfn)(void *data),
						 void *data, const char *name)
{
	struct task_struct *task;

	task = calloc(sizeof(*task), 1);
	if (!task)
		return ERR_PTR(-ENOMEM);

	task->threadfn = threadfn;
	task->data = data;
	task->poller.func = kthread_poller;
	task->poller.name = strdup(name);

	return task;
}

static inline int wake_up_process(struct task_struct *task)
{
	return poller_register(&task->poller, task->poller.name);
}

/**
 * kthread_run - create and wake a thread.
 * @threadfn: the function to run until signal_pending(current).
 * @data: data ptr for @threadfn.
 * @name: name for the thread.
 *
 * Description: Convenient wrapper for kthread_create() followed by
 * wake_up_process().  Returns the kthread or ERR_PTR(-ENOMEM).
 */
#define kthread_run(threadfn, data, name)				   \
({									   \
	struct task_struct *__k						   \
		= kthread_create(threadfn, data, name);			   \
	if (!IS_ERR(__k))						   \
		wake_up_process(__k);					   \
	__k;								   \
})

static inline void free_kthread_struct(struct task_struct *k)
{
	if (!k)
		return;

	poller_unregister(&k->poller);
	free(k);
}

#endif /* _LINUX_KTHREAD_H */
