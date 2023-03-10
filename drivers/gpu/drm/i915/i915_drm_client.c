// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <drm/drm_print.h>

#include <uapi/drm/i915_drm.h>

#include "gem/i915_gem_context.h"
#include "gt/intel_engine_user.h"
#include "gt/intel_gt.h"

#include "i915_drm_client.h"
#include "i915_drv.h"
#include "i915_gem.h"
#include "i915_utils.h"
#include "i915_debugger.h"

struct i915_drm_client_bo {
	struct rb_node node;
	struct i915_drm_client *client;
	unsigned int count;
	bool shared;
};

/* compat: 2efc459d06f1 ("sysfs: Add sysfs_emit and sysfs_emit_at to format sysfs output") */
#define sysfs_emit(buf, fmt...) scnprintf(buf, PAGE_SIZE, fmt)

void i915_drm_clients_init(struct i915_drm_clients *clients,
			   struct drm_i915_private *i915)
{
	clients->i915 = i915;
	clients->wq = create_workqueue("i915_drm_clients");

	clients->next_id = 0;
	xa_init_flags(&clients->xarray, XA_FLAGS_ALLOC);
}

static ssize_t
show_client_name(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct i915_drm_client *client =
		container_of(attr, typeof(*client), attr.name);
	int ret;

	rcu_read_lock();
	ret = sysfs_emit(buf,
			 READ_ONCE(client->closed) ? "<%s>\n" : "%s\n",
			 i915_drm_client_name(client));
	rcu_read_unlock();

	return ret;
}

static ssize_t
show_client_pid(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct i915_drm_client *client =
		container_of(attr, typeof(*client), attr.pid);
	int ret;

	rcu_read_lock();
	ret = sysfs_emit(buf,
			 READ_ONCE(client->closed) ? "<%u>\n" : "%u\n",
			 pid_nr(i915_drm_client_pid(client)));
	rcu_read_unlock();

	return ret;
}

static u64 busy_add(struct i915_gem_context *ctx, unsigned int class)
{
	struct i915_gem_engines_iter it;
	struct intel_context *ce;
	u64 total = 0;

	for_each_gem_engine(ce, rcu_dereference(ctx->engines), it) {
		if (ce->engine->uabi_class != class)
			continue;

		total += intel_context_get_total_runtime_ns(ce);
	}

	return total;
}

static ssize_t
show_busy(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct i915_engine_busy_attribute *i915_attr =
		container_of(attr, typeof(*i915_attr), attr);
	unsigned int class = i915_attr->engine_class;
	const struct i915_drm_client *client = i915_attr->client;
	const struct list_head *list = &client->ctx_list;
	u64 total = atomic64_read(&client->past_runtime[class]);
	struct i915_gem_context *ctx;

	rcu_read_lock();
	list_for_each_entry_rcu(ctx, list, client_link)
		total += busy_add(ctx, class);
	rcu_read_unlock();

	return sysfs_emit(buf, "%llu\n", total);
}

/*
 * The objs created by a client which have a possible placement in Local
 * Memory only are accounted. Their sizes are aggregated and presented via
 * this sysfs entry
 */
static ssize_t show_client_created_devm_bytes(struct device *kdev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct i915_drm_client *client =
		container_of(attr, typeof(*client), attr.created_devm_bytes);

	return sysfs_emit(buf, "%llu\n",
			  atomic64_read(&client->created_devm_bytes));
}

static ssize_t show_client_resident_created_devm_bytes(struct device *kdev,
						       struct device_attribute *attr,
						       char *buf)
{
	struct i915_drm_client *client =
		container_of(attr, typeof(*client), attr.resident_created_devm_bytes);

	return sysfs_emit(buf, "%llu\n",
			  atomic64_read(&client->resident_created_devm_bytes));
}

/*
 * The objs imported by a client via PRIME/FLINK which have a possible
 * placement in Local  Memory only are accounted. Their sizes are aggregated
 * and presented via this sysfs entry
 */
static ssize_t show_client_imported_devm_bytes(struct device *kdev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct i915_drm_client *client =
		container_of(attr, typeof(*client), attr.imported_devm_bytes);

	return sysfs_emit(buf, "%llu\n",
			  atomic64_read(&client->imported_devm_bytes));
}

static ssize_t show_client_resident_imported_devm_bytes(struct device *kdev,
							struct device_attribute *attr,
							char *buf)
{
	struct i915_drm_client *client =
		container_of(attr, typeof(*client), attr.resident_imported_devm_bytes);

	return sysfs_emit(buf, "%llu\n",
			  atomic64_read(&client->resident_imported_devm_bytes));
}

static const char * const uabi_class_names[] = {
	[I915_ENGINE_CLASS_RENDER] = "0",
	[I915_ENGINE_CLASS_COPY] = "1",
	[I915_ENGINE_CLASS_VIDEO] = "2",
	[I915_ENGINE_CLASS_VIDEO_ENHANCE] = "3",
	[I915_ENGINE_CLASS_COMPUTE] = "4",
};

static int __client_register_sysfs_busy(struct i915_drm_client *client)
{
	struct i915_drm_clients *clients = client->clients;
	unsigned int i;
	int ret = 0;

	client->busy_root = kobject_create_and_add("busy", client->root);
	if (!client->busy_root)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(uabi_class_names); i++) {
		struct i915_engine_busy_attribute *i915_attr =
			&client->attr.busy[i];
		struct device_attribute *attr = &i915_attr->attr;

		if (!intel_engine_lookup_user(clients->i915, i, 0))
			continue;

		i915_attr->client = client;
		i915_attr->engine_class = i;

		sysfs_attr_init(&attr->attr);

		attr->attr.name = uabi_class_names[i];
		attr->attr.mode = 0444;
		attr->show = show_busy;

		ret = sysfs_create_file(client->busy_root, &attr->attr);
		if (ret)
			goto out;
	}

out:
	if (ret)
		kobject_put(client->busy_root);

	return ret;
}

static void __client_unregister_sysfs_busy(struct i915_drm_client *client)
{
	kobject_put(fetch_and_zero(&client->busy_root));
}

void i915_drm_client_init_bo(struct drm_i915_gem_object *obj)
{
	spin_lock_init(&obj->client.lock);
	obj->client.rb = RB_ROOT;
}

static bool object_has_lmem(const struct drm_i915_gem_object *obj)
{
	int i;

	for (i = 0; i < obj->mm.n_placements; i++) {
		struct intel_memory_region *placement = obj->mm.placements[i];

		if (placement->type == INTEL_MEMORY_LOCAL)
			return true;
	}

	return false;
}

static int sort_client_key(const void *key, const struct rb_node *node)
{
	const struct i915_drm_client_bo *cb = rb_entry(node, typeof(*cb), node);

	return ptrdiff(key, cb->client);
}

static int sort_client(struct rb_node *node, const struct rb_node *parent)
{
	const struct i915_drm_client_bo *cb = rb_entry(node, typeof(*cb), node);

	return sort_client_key(cb->client, parent);
}

int i915_drm_client_add_bo(struct i915_drm_client *client,
			   struct drm_i915_gem_object *obj)
{
	struct i915_drm_client_bo *cb;
	struct rb_node *old;

	/* only objs which can reside in LOCAL MEMORY are tracked */
	if (!object_has_lmem(obj))
		return 0;

	cb = kzalloc(sizeof(*cb), GFP_KERNEL);
	if (!cb)
		return -ENOMEM;

	cb->client = client;
	cb->shared = obj->base.dma_buf;

	spin_lock(&obj->client.lock);

	old = rb_find_add(&cb->node, &obj->client.rb, sort_client);
	if (old) {
		kfree(cb);
		cb = rb_entry(old, typeof(*cb), node);
	} else {
		if (cb->shared)
			atomic64_add(obj->base.size,
				     &client->imported_devm_bytes);
		else
			atomic64_add(obj->base.size,
				     &client->created_devm_bytes);

		if (obj->client.resident) {
			if (cb->shared)
				atomic64_add(obj->base.size,
					     &client->resident_imported_devm_bytes);
			else
				atomic64_add(obj->base.size,
					     &client->resident_created_devm_bytes);
		}
	}

	cb->count++;
	spin_unlock(&obj->client.lock);

	return 0;
}

void i915_drm_client_make_resident(struct drm_i915_gem_object *obj,
				   bool resident)
{
	struct i915_drm_client_bo *cb, *n;
	int64_t sz;

	sz = obj->base.size;
	if (!resident)
		sz = -sz;

	spin_lock(&obj->client.lock);
	if (obj->client.resident != resident) {
		obj->client.resident = resident;
		rbtree_postorder_for_each_entry_safe(cb, n, &obj->client.rb, node) {
			if (cb->shared)
				atomic64_add(sz, &cb->client->resident_imported_devm_bytes);
			else
				atomic64_add(sz, &cb->client->resident_created_devm_bytes);
		}
	}
	spin_unlock(&obj->client.lock);
}

static struct i915_drm_client_bo *
lookup_client(struct i915_drm_client *client,
	      struct drm_i915_gem_object *obj)
{
	struct rb_node *rb = rb_find(client, &obj->client.rb, sort_client_key);

	GEM_BUG_ON(offsetof(struct i915_drm_client_bo, node));
	return rb_entry(rb, struct i915_drm_client_bo, node);
}

void i915_drm_client_del_bo(struct i915_drm_client *client,
			    struct drm_i915_gem_object *obj)
{
	struct i915_drm_client_bo *cb;

	spin_lock(&obj->client.lock);
	cb = lookup_client(client, obj);
	if (cb && !--cb->count) {
		if (cb->shared)
			atomic64_sub(obj->base.size,
				     &client->imported_devm_bytes);
		else
			atomic64_sub(obj->base.size,
				     &client->created_devm_bytes);

		if (obj->client.resident) {
			if (cb->shared)
				atomic64_sub(obj->base.size,
					     &client->resident_imported_devm_bytes);
			else
				atomic64_sub(obj->base.size,
					     &client->resident_created_devm_bytes);
		}

		rb_erase(&cb->node, &obj->client.rb);
		kfree(cb);
	}
	spin_unlock(&obj->client.lock);
}

void i915_drm_client_fini_bo(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!RB_EMPTY_ROOT(&obj->client.rb));
}

static int
i915_drm_client_get_object_priority(struct drm_i915_gem_object *obj, int *highest_priority, int *nb_priorities ) 
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_drm_client *client;
	unsigned long idx;
	int prioList[256] = { -1 };
	int obj_priority = -1;
	int count = 0;

	*highest_priority = -1;

	rcu_read_lock();

	xa_for_each(&i915->clients.xarray, idx, client) {
		struct i915_drm_client_bo *cb;
		struct task_struct *task;  
		int client_priority;
		int i;
		bool found = false;

		if (READ_ONCE(client->closed))
			continue;

		client = i915_drm_client_get_rcu(client);

		if (!client )
			continue;
			
		task = pid_task(i915_drm_client_pid(client), PIDTYPE_PID);
		
		if (true) { //task_is_running(task)
			client_priority = 19 - task_nice(task);
			
			for ( i=0; i<count; i++) {
				if ( prioList[i] == client_priority ) {
					found = true;
					break;
				}
			}

			if ( !found ) {
				prioList[count++] = client_priority;
			}

			if ( client_priority > *highest_priority ) {
				*highest_priority = client_priority;
			}

			spin_lock(&obj->client.lock);

			cb = lookup_client(client, obj);
			if (cb) {
				obj_priority = client_priority;
			}
			
			spin_unlock(&obj->client.lock);
		}

		i915_drm_client_put(client);
	}

	*nb_priorities = count;
	
	rcu_read_unlock();



	return obj_priority;
}

bool i915_drm_client_can_object_be_swapped_out(struct drm_i915_gem_object *obj, bool *force)
{
	bool ret = true;
	int obj_priority;
	int highest_priority;
	int count;

	obj_priority = i915_drm_client_get_object_priority(obj, &highest_priority, &count);

	if ( obj_priority > 0 && highest_priority > 0  && count > 1) {
		if ( obj_priority < highest_priority ) {
			*force = true;
		}
		else {
			ret = false;
		}
	}

	return ret ;
}

bool i915_drm_client_can_object_be_swapped_in(struct drm_i915_gem_object *obj)
{
	bool ret = true;
	int obj_priority;
	int highest_priority;
	int count;

	obj_priority = i915_drm_client_get_object_priority(obj, &highest_priority, &count);

	if ( obj_priority > 0 && highest_priority > 0  && count > 1) {
		if ( obj_priority < highest_priority ) {
			ret = false;
		}
	}
	
	return ret ;
}


static int
__client_register_sysfs_memory_stats(struct i915_drm_client *client)
{
	const struct {
		const char *name;
		struct device_attribute *attr;
		ssize_t (*show)(struct device *dev,
				struct device_attribute *attr,
				char *buf);
	} files[] = {
		{
			"created_bytes",
			&client->attr.created_devm_bytes,
			show_client_created_devm_bytes
		},
		{
			"imported_bytes",
			&client->attr.imported_devm_bytes,
			show_client_imported_devm_bytes
		},
		{
			"prelim_resident_created_bytes",
			&client->attr.resident_created_devm_bytes,
			show_client_resident_created_devm_bytes
		},
		{
			"prelim_resident_imported_bytes",
			&client->attr.resident_imported_devm_bytes,
			show_client_resident_imported_devm_bytes
		},
	};
	unsigned int i;
	int ret;

	client->devm_stats_root =
		kobject_create_and_add("total_device_memory_buffer_objects",
				       client->root);
	if (!client->devm_stats_root)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(files); i++) {
		struct device_attribute *attr = files[i].attr;

		sysfs_attr_init(&attr->attr);

		attr->attr.name = files[i].name;
		attr->attr.mode = 0444;
		attr->show = files[i].show;

		ret = sysfs_create_file(client->devm_stats_root,
					(struct attribute *)attr);
		if (ret)
			goto out;
	}
out:
	if (ret)
		kobject_put(client->devm_stats_root);

	return ret;
}

static void
__client_unregister_sysfs_memory_stats(struct i915_drm_client *client)
{
	kobject_put(fetch_and_zero(&client->devm_stats_root));
}

static int __client_register_sysfs(struct i915_drm_client *client)
{
	const struct {
		const char *name;
		struct device_attribute *attr;
		ssize_t (*show)(struct device *dev,
				struct device_attribute *attr,
				char *buf);
	} files[] = {
		{ "name", &client->attr.name, show_client_name },
		{ "pid", &client->attr.pid, show_client_pid },
	};
	unsigned int i;
	char buf[16];
	int ret;

	ret = scnprintf(buf, sizeof(buf), "%u", client->id);
	if (ret == sizeof(buf))
		return -EINVAL;

	client->root = kobject_create_and_add(buf, client->clients->root);
	if (!client->root)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(files); i++) {
		struct device_attribute *attr = files[i].attr;

		sysfs_attr_init(&attr->attr);

		attr->attr.name = files[i].name;
		attr->attr.mode = 0444;
		attr->show = files[i].show;

		ret = sysfs_create_file(client->root, &attr->attr);
		if (ret)
			goto out;
	}

	ret = __client_register_sysfs_busy(client);
	if (ret)
		goto out;

	ret = __client_register_sysfs_memory_stats(client);

out:
	if (ret)
		kobject_put(client->root);

	return ret;
}

static void __client_unregister_sysfs(struct i915_drm_client *client)
{
	__client_unregister_sysfs_busy(client);
	__client_unregister_sysfs_memory_stats(client);

	kobject_put(fetch_and_zero(&client->root));
}

static struct i915_drm_client_name *get_name(struct i915_drm_client *client,
					     struct task_struct *task)
{
	struct i915_drm_client_name *name;
	int len = strlen(task->comm);

	name = kmalloc(struct_size(name, name, len + 1), GFP_KERNEL);
	if (!name)
		return NULL;

	init_rcu_head(&name->rcu);
	name->client = client;
	name->pid = get_task_pid(task, PIDTYPE_PID);
	memcpy(name->name, task->comm, len + 1);

	return name;
}

static void free_name(struct rcu_head *rcu)
{
	struct i915_drm_client_name *name =
		container_of(rcu, typeof(*name), rcu);

	put_pid(name->pid);
	kfree(name);
}

static int
__i915_drm_client_register(struct i915_drm_client *client,
			   struct task_struct *task)
{
	struct i915_drm_clients *clients = client->clients;
	struct i915_drm_client_name *name;
	int ret;

	name = get_name(client, task);
	if (!name)
		return -ENOMEM;

	RCU_INIT_POINTER(client->name, name);

	if (!clients->root)
		return 0; /* intel_fbdev_init registers a client before sysfs */

	ret = __client_register_sysfs(client);
	if (ret)
		goto err_sysfs;

	return 0;

err_sysfs:
	RCU_INIT_POINTER(client->name, NULL);
	call_rcu(&name->rcu, free_name);
	return ret;
}

static void __i915_drm_client_unregister(struct i915_drm_client *client)
{
	struct i915_drm_client_name *name;

	__client_unregister_sysfs(client);

	mutex_lock(&client->update_lock);
	name = rcu_replace_pointer(client->name, NULL, true);
	mutex_unlock(&client->update_lock);

	call_rcu(&name->rcu, free_name);
}

static void __rcu_i915_drm_client_free(struct work_struct *wrk)
{
	struct i915_drm_client *client =
		container_of(wrk, typeof(*client), rcu.work);
	struct drm_i915_private *i915 = client->clients->i915;

	__i915_drm_client_unregister(client);

	xa_erase(&client->clients->xarray, client->id);
	pvc_wa_allow_rc6(i915);
	i915_uuid_cleanup(client);

	kfree(client);
}

struct i915_drm_client *
i915_drm_client_add(struct i915_drm_clients *clients,
		    struct task_struct *task,
		    struct drm_i915_file_private *file)
{
	struct drm_i915_private *i915 = clients->i915;
	struct i915_drm_client *client;
	int ret;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	kref_init(&client->kref);
	mutex_init(&client->update_lock);
	spin_lock_init(&client->ctx_lock);
	INIT_LIST_HEAD(&client->ctx_list);

	client->file = file;

	client->clients = clients;
	INIT_RCU_WORK(&client->rcu, __rcu_i915_drm_client_free);
	pvc_wa_disallow_rc6(i915);

	i915_debugger_wait_on_discovery(clients->i915, NULL);

	ret = xa_alloc_cyclic(&clients->xarray, &client->id, client,
			      xa_limit_32b, &clients->next_id, GFP_KERNEL);
	if (ret < 0)
		goto err_id;

	ret = __i915_drm_client_register(client, task);
	if (ret)
		goto err_register;

	GEM_WARN_ON(task != current);
	i915_debugger_client_register(client, current);
	i915_debugger_client_create(client);

	i915_uuid_init(client);
	return client;

err_register:
	xa_erase(&clients->xarray, client->id);
err_id:
	kfree(client);

	return ERR_PTR(ret);
}

void __i915_drm_client_free(struct kref *kref)
{
	struct i915_drm_client *client =
		container_of(kref, typeof(*client), kref);

	queue_rcu_work(client->clients->wq, &client->rcu);
}

void i915_drm_client_close(struct i915_drm_client *client)
{
	GEM_BUG_ON(READ_ONCE(client->closed));
	WRITE_ONCE(client->closed, true);
	i915_debugger_client_destroy(client);
	i915_drm_client_put(client);
}

int
i915_drm_client_update(struct i915_drm_client *client,
		       struct task_struct *task)
{
	struct i915_drm_client_name *name;

	name = get_name(client, task);
	if (!name)
		return -ENOMEM;

	mutex_lock(&client->update_lock);
	if (name->pid != rcu_dereference_protected(client->name, true)->pid)
		name = rcu_replace_pointer(client->name, name, true);
	mutex_unlock(&client->update_lock);

	call_rcu(&name->rcu, free_name);
	return 0;
}

void i915_drm_clients_fini(struct i915_drm_clients *clients)
{
	flush_workqueue(clients->wq);

	if (!xa_empty(&clients->xarray)) {
		rcu_barrier();
		flush_workqueue(clients->wq);
	}

	xa_destroy(&clients->xarray);
	destroy_workqueue(clients->wq);
}
