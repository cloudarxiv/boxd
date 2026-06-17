#ifndef _LINUX_UVM_CTRL_
#define _LINUX_UVM_CTRL_

#include "linux/cgroup-defs.h"
#include <linux/export.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/uvm_ctrl.h>
#include <linux/pci.h>
#include <linux/sprintf.h>

static struct uvm_ctrl_css root_cg;
static unsigned long flags;
static char *inp_str = "%u %llu";

static int count_gpus(void) {
	struct pci_dev *pdev = NULL;
	int gpu_count = 0;

	while((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev))) {
		if(pdev->class >> 16 == PCI_BASE_CLASS_DISPLAY && pdev->vendor == PCI_VENDOR_ID_NVIDIA) {
			gpu_count++;
		}
	}
	return gpu_count;
}

static void lock_before_call(void)
{
	if (!lock_initialized) {
		spin_lock_init(&callback_spin_lock);
	}
	spin_lock_irqsave(&callback_spin_lock, flags);
}

static void unlock_after_call(void)
{
	spin_unlock_irqrestore(&callback_spin_lock, flags);
}

/* On allocation */
static struct cgroup_subsys_state *
uvm_ctrl_cgroup_alloc(struct cgroup_subsys_state *parent_css)
{
	struct uvm_ctrl_css *cg;

	if (!parent_css) {
		cg = &root_cg;
	} else {
		cg = kzalloc(sizeof(*cg), GFP_KERNEL);
		if (!cg){
			pr_err("Failed to allocate UVM CSS!\n");
			return ERR_PTR(-ENOMEM);
		}
	}
	INIT_LIST_HEAD(&cg->gpu_css);

	/* Here I may want to add something to default init it idk */
	// lock_before_call();
	// if(uvm_ctrl_callback_func != NULL) {
	// 	pr_info("Callback to new css\n");
	// 	(*uvm_ctrl_callback_func)(UVM_NEW_CSS);
	// } else{
	// 	pr_warn("Callaback not found alloc css!\n");
	// }
	// unlock_after_call();
	int num_gpus = count_gpus();
	for(int i = 0; i<num_gpus; ++i) {
		struct uvm_gpu_specific *uvm_gpu = kzalloc(sizeof(struct uvm_gpu_specific), GFP_KERNEL);
		if(IS_ERR(uvm_gpu)){
			pr_err("Failed to allocate gpu_specific structs");
			return ERR_PTR(-ENOMEM);
		}

		uvm_gpu->res[UVM_SOFT_LIMIT] = DEFAULT_SOFT_LIMIT;
		uvm_gpu->res[UVM_HARD_LIMIT] = DEFAULT_HARD_LIMIT;
		uvm_gpu->gpu_id = i;
		list_add_tail(&uvm_gpu->node, &cg->gpu_css);
		pr_info("Added GPU with id %u", i);
	}
	
	return &cg->css;
}

void uvm_ctrl_register_callback(void (*func)(struct uvm_ctrl_callback_info))
{
	uvm_ctrl_callback_func = func;
	pr_info("Registered func\n");
}
EXPORT_SYMBOL_GPL(uvm_ctrl_register_callback);

void uvm_ctrl_unregister_callback(void)
{
	uvm_ctrl_callback_func = NULL;
}
EXPORT_SYMBOL_GPL(uvm_ctrl_unregister_callback);

static void uvm_ctrl_cgroup_free(struct cgroup_subsys_state *css)
{
	// lock_before_call();
	// if(uvm_ctrl_callback_func != NULL) {
	// 	pr_info("Callback to new css\n");
	// 	(*uvm_ctrl_callback_func)(UVM_CSS_GONE);
	// } else {
	// 	pr_warn("Callaback not found dead css!\n");
	// }
	// unlock_after_call();
        struct uvm_ctrl_css *cg = css_to_uvm_css(css);
        struct uvm_gpu_specific *uvm_gpu, *tmp;

        list_for_each_entry_safe(uvm_gpu, tmp, &cg->gpu_css, node) {
          list_del(&uvm_gpu->node);
          kfree(uvm_gpu);
        }
        kfree(css_to_uvm_css(css));
}

static struct uvm_ctrl_css *parent_uvm_css(struct uvm_ctrl_css *cgroup)
{
	return cgroup ? css_to_uvm_css(cgroup->css.parent) : NULL;
}

static inline bool valid_type(enum uvm_ctrl_type type)
{
	return type >= 0 && type <= UVM_CTRL_TYPES;
}

static int uvm_ctrl_soft_limit_show(struct seq_file *sf, void *args)
{
	struct cgroup_subsys_state *css = seq_css(sf);
	struct uvm_ctrl_css *cg = css_to_uvm_css(css);
	struct uvm_gpu_specific *uvm_gpu;
	list_for_each_entry(uvm_gpu, &cg->gpu_css, node) {
		seq_printf(sf, "GPU: %u, SOFT LIMIT %llu\n",uvm_gpu->gpu_id, uvm_gpu->res[UVM_SOFT_LIMIT]);
	}
	return 0;
}

static ssize_t uvm_ctrl_soft_limit_write(struct kernfs_open_file *of, char *buf,
					 size_t nbytes, loff_t off)
{
	struct uvm_ctrl_css *cg;

	buf = strstrip(buf);
	u64 new_limit;
	u32 gpu_id;
	int ret;
	if (nbytes == 0)
		return 0;

	ret = sscanf(buf,inp_str, &gpu_id, &new_limit);
	if (ret != 2) {
		pr_warn("Unexpected number of arguments <gpu_id> <soft_limit>");
		return -EINVAL;
	}

	struct cgroup_subsys_state *css = of_css(of);
	cg = css_to_uvm_css(css);
	struct uvm_gpu_specific *uvm_gpu;
	list_for_each_entry(uvm_gpu, &cg->gpu_css, node) {
		if(uvm_gpu->gpu_id == gpu_id){
			WRITE_ONCE(uvm_gpu->res[UVM_SOFT_LIMIT], new_limit);
			break;
		}
	}
	lock_before_call();
	if (uvm_ctrl_callback_func != NULL) {
		pr_info("Callback to new soft limit\n");
		struct uvm_ctrl_callback_info callback_info;
		callback_info.type = UVM_SOFT_LIMIT_CHANGED;
		callback_info.css = of_css(of);
		callback_info.soft_limit = new_limit;
		callback_info.gpu_id = gpu_id;
		(*uvm_ctrl_callback_func)(callback_info);
	} else {
		pr_warn("Callaback not found new soft limit!\n");
	}
	unlock_after_call();
	return nbytes;
}

static ssize_t uvm_ctrl_hard_limit_write(struct kernfs_open_file *of, char *buf,
                                         size_t nbytes, loff_t off) {
  struct uvm_ctrl_css *cg;

  buf = strstrip(buf);

  u64 new_limit;
  u32 gpu_id;
  int ret;
  if (nbytes == 0)
    return 0;

  ret = sscanf(buf, inp_str, &gpu_id, &new_limit);
  if (ret != 2) {
    pr_warn("Unexpected number of arguments <gpu_id> <hard_limit>");
    return -EINVAL;
  }

  struct cgroup_subsys_state *css = of_css(of);
  cg = css_to_uvm_css(css);
  struct uvm_gpu_specific *uvm_gpu;
  list_for_each_entry(uvm_gpu, &cg->gpu_css, node) {
    if (uvm_gpu->gpu_id == gpu_id) {
      WRITE_ONCE(uvm_gpu->res[UVM_HARD_LIMIT], new_limit);
      break;
    }
  }
  lock_before_call();
  if (uvm_ctrl_callback_func != NULL) {
    struct uvm_ctrl_callback_info callback_info;
    callback_info.type = UVM_HARD_LIMIT_CHANGED;
    callback_info.css = of_css(of);
    callback_info.hard_limit = new_limit;
    callback_info.gpu_id = gpu_id;
    (*uvm_ctrl_callback_func)(callback_info);
  } else {
    pr_warn("Callaback not found hard limit change!\n");
  }
  unlock_after_call();
  return nbytes;
}

static int uvm_ctrl_hard_limit_show(struct seq_file *sf, void *args)
{
	struct cgroup_subsys_state *css = seq_css(sf);
	struct uvm_ctrl_css *cg = css_to_uvm_css(css);
	struct uvm_gpu_specific *uvm_gpu;
	list_for_each_entry(uvm_gpu, &cg->gpu_css, node) {
		seq_printf(sf, "GPU: %u, HARD LIMIT %llu\n",uvm_gpu->gpu_id, uvm_gpu->res[UVM_HARD_LIMIT]);
	}
	return 0;
}

static  int show_gpu_count(struct seq_file *sf, void *args) {
	int gpu_count = count_gpus();
	seq_printf(sf, "GPUs Detected %d\n", gpu_count);
	return 0;
}

static void got_new_task(struct task_struct *task)
{
	// lock_before_call();
	if(uvm_ctrl_callback_func != NULL) {
		struct uvm_ctrl_callback_info callback_info;
		callback_info.type = UVM_NEW_TASK;
		callback_info.css = task_get_css(task, uvm_ctrl_get_subsys_id()); 
		callback_info.tsk  = task;
		(*uvm_ctrl_callback_func)(callback_info);
		css_put(callback_info.css);
	} else {
		// pr_warn("Callaback not found new task!\n");
	}
	// unlock_after_call();
}

static void old_task_exit(struct task_struct *task)
{
	// lock_before_call();
	if(uvm_ctrl_callback_func != NULL) {
		struct uvm_ctrl_callback_info callback_info;
		callback_info.type = UVM_EXIT_TASK;
		callback_info.css = task_get_css(task, uvm_ctrl_get_subsys_id()); 
		callback_info.tsk  = task;
		(*uvm_ctrl_callback_func)(callback_info);
		css_put(callback_info.css);
	} else {
		// pr_warn("Callaback not found old task dead!\n");
	}
	// unlock_after_call();
}

static void css_dead(struct cgroup_subsys_state *css)
{
	lock_before_call();
	if (uvm_ctrl_callback_func != NULL) {
		pr_info("Css died\n");
		struct uvm_ctrl_callback_info callback_info;
		callback_info.type = UVM_CSS_GONE;
		callback_info.css = css; 
		(*uvm_ctrl_callback_func)(callback_info);
	} else {
		pr_warn("callback for dead css not available\n");
	}
	unlock_after_call();
}

static int css_ready(struct cgroup_subsys_state *css)
{
	lock_before_call();
	if (uvm_ctrl_callback_func != NULL) {
		pr_info("Css ALIVE\n");
		struct uvm_ctrl_callback_info callback_info;
		callback_info.type = UVM_NEW_CSS;
		callback_info.css = css; 
		(*uvm_ctrl_callback_func)(callback_info);
	} else {
		pr_warn("callback for new css not available\n");
	}
	unlock_after_call();
	return 0;
}

static struct cftype gpu_count_files[] = {
    {
        .name = "gpu_count",
        .flags = CFTYPE_ONLY_ON_ROOT,
        .seq_show = show_gpu_count,
    },

    {
				.name = "soft",
        .seq_show = uvm_ctrl_soft_limit_show,
        .write = uvm_ctrl_soft_limit_write,
    },
    {
				.name = "hard",
        .seq_show = uvm_ctrl_hard_limit_show,
        .write = uvm_ctrl_hard_limit_write,
    },

};

struct cgroup_subsys uvm_ctrl_cgrp_subsys = {
	.css_alloc = uvm_ctrl_cgroup_alloc,
	.css_free = uvm_ctrl_cgroup_free,
	.dfl_cftypes = gpu_count_files,
	.legacy_cftypes = gpu_count_files,
	// .fork = got_new_task,
	// .exit = old_task_exit,
	.css_online = css_ready,
	.css_offline = css_dead,
};

#endif
