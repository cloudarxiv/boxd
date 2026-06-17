#ifndef _LINUX_UVM_CTRL_H
#define _LINUX_UVM_CTRL_H

#include <linux/kernel.h>
#include <linux/cgroup-defs.h>
#include <linux/cgroup.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/list.h>

enum uvm_ctrl_type {
	UVM_SOFT_LIMIT,
	UVM_HARD_LIMIT,
	UVM_CTRL_TYPES,
};

enum uvm_ctrl_callback_type {
	UVM_NEW_CSS,
	UVM_CSS_GONE,
	UVM_PROC_MOVED,
	UVM_SOFT_LIMIT_CHANGED,
	UVM_HARD_LIMIT_CHANGED,
	UVM_NEW_TASK,
	UVM_EXIT_TASK,
};

static u64 DEFAULT_SOFT_LIMIT = 1ULL << 30;
static u64 DEFAULT_HARD_LIMIT = 2ULL << 30;

struct uvm_ctrl_callback_info {
	// css pointer for new css or deleting css
	// I can also do id here
	struct cgroup_subsys_state *css;

	// for new and old task
	struct task_struct *tsk;

	// new soft limit
	u64 soft_limit;

	// new hard limit
	u64 hard_limit;

	u32 gpu_id;

	enum uvm_ctrl_callback_type type;
};

static void (*uvm_ctrl_callback_func)(struct uvm_ctrl_callback_info) = NULL;
static spinlock_t callback_spin_lock;
static bool lock_initialized = false;

void uvm_ctrl_register_callback(void (*func)(struct uvm_ctrl_callback_info));
void uvm_ctrl_unregister_callback(void);


struct uvm_gpu_specific {
	int gpu_id;
	struct list_head node;
	u64 res[UVM_CTRL_TYPES];
};


struct uvm_ctrl_css {
	struct cgroup_subsys_state css;
	struct list_head gpu_css;
};

static inline struct uvm_ctrl_css *
css_to_uvm_css(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct uvm_ctrl_css, css) : NULL;
}

static inline int uvm_ctrl_get_subsys_id(void)
{
	return uvm_ctrl_cgrp_id;
}

#endif
