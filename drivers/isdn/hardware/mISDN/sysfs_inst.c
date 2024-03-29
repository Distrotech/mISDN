/* $Id: sysfs_inst.c,v 1.10 2006/09/06 17:24:22 crich Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * mISDN sysfs stuff for isnstances
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#include <linux/module.h>
#include "core.h"
#include "sysfs.h"

#define to_mISDNinstance(d) container_of(d, mISDNinstance_t, class_dev)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
static ssize_t show_inst_id(struct device *class_dev, struct device_attribute *attr, char *buf) {
        mISDNinstance_t *inst = to_mISDNinstance(class_dev);
        return sprintf(buf, "%08x\n", inst->id);
}
static DEVICE_ATTR(id, S_IRUGO, show_inst_id, NULL);

static ssize_t show_inst_name(struct device *class_dev, struct device_attribute *attr, char *buf) {
        mISDNinstance_t *inst = to_mISDNinstance(class_dev);
        return sprintf(buf, "%s\n", inst->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_inst_name, NULL);

static ssize_t show_inst_extentions(struct device *class_dev, struct device_attribute *attr, char *buf)
{
        mISDNinstance_t *inst = to_mISDNinstance(class_dev);
        return sprintf(buf, "%08x\n", inst->extentions);
}
static DEVICE_ATTR(extentions, S_IRUGO, show_inst_extentions, NULL);

static ssize_t show_inst_regcnt(struct device *class_dev, struct device_attribute *attr, char *buf)
{
        mISDNinstance_t *inst = to_mISDNinstance(class_dev);
        return sprintf(buf, "%d\n", inst->regcnt);
}
static DEVICE_ATTR(regcnt, S_IRUGO, show_inst_regcnt, NULL);

#else
static ssize_t show_inst_id(struct class_device *class_dev, char *buf) {
	mISDNinstance_t	*inst = to_mISDNinstance(class_dev);
	return sprintf(buf, "%08x\n", inst->id);
}
static CLASS_DEVICE_ATTR(id, S_IRUGO, show_inst_id, NULL);

static ssize_t show_inst_name(struct class_device *class_dev, char *buf) {
	mISDNinstance_t	*inst = to_mISDNinstance(class_dev);
	return sprintf(buf, "%s\n", inst->name);
}
static CLASS_DEVICE_ATTR(name, S_IRUGO, show_inst_name, NULL);

static ssize_t show_inst_extentions(struct class_device *class_dev, char *buf)
{
	mISDNinstance_t	*inst = to_mISDNinstance(class_dev);
	return sprintf(buf, "%08x\n", inst->extentions);
}
static CLASS_DEVICE_ATTR(extentions, S_IRUGO, show_inst_extentions, NULL);

static ssize_t show_inst_regcnt(struct class_device *class_dev, char *buf)
{
	mISDNinstance_t	*inst = to_mISDNinstance(class_dev);
	return sprintf(buf, "%d\n", inst->regcnt);
}
static CLASS_DEVICE_ATTR(regcnt, S_IRUGO, show_inst_regcnt, NULL);
#endif

#ifdef SYSFS_SUPPORT
MISDN_PROTO(mISDNinstance, pid, S_IRUGO);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
static void release_mISDN_inst(struct device *dev) {
#ifdef SYSFS_SUPPORT
        mISDNinstance_t *inst = to_mISDNinstance(dev);

        if (inst->obj)
                sysfs_remove_link(&dev->kobj, "obj");
        sysfs_remove_group(&inst->class_dev.kobj, &pid_group);
#endif
        if (core_debug & DEBUG_SYSFS)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
		printk(KERN_INFO "release instance class dev %s\n", dev_name(dev));
#else
                printk(KERN_INFO "release instance class dev %s\n", dev->class_id);
#endif
}

#else
static void release_mISDN_inst(struct class_device *dev) {
#ifdef SYSFS_SUPPORT
	mISDNinstance_t	*inst = to_mISDNinstance(dev);

	if (inst->obj)
		sysfs_remove_link(&dev->kobj, "obj");
	sysfs_remove_group(&inst->class_dev.kobj, &pid_group);
#endif
	if (core_debug & DEBUG_SYSFS)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
		printk(KERN_INFO "release instance class dev %s\n", dev->bus_id);
#else
		printk(KERN_INFO "release instance class dev %s\n", dev->class_id);
#endif
}
#endif

static struct class inst_dev_class = {
	.name		= "mISDN-instances",
#ifndef CLASS_WITHOUT_OWNER
	.owner		= THIS_MODULE,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
	.dev_release	= &release_mISDN_inst,
#else
	.release	= &release_mISDN_inst
#endif
};

int
mISDN_register_sysfs_inst(mISDNinstance_t *inst) {
	int	err;
#ifdef SYSFS_SUPPORT
	char	name[8];
#endif

	inst->class_dev.class = &inst_dev_class;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
	dev_set_name(&inst->class_dev, "inst-%08x", inst->id);
	err = device_register(&inst->class_dev);
#else
	snprintf(inst->class_dev.class_id, BUS_ID_SIZE, "inst-%08x", inst->id);
	err = class_device_register(&inst->class_dev);
#endif
	if (err)
		return(err);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
        device_create_file(&inst->class_dev, &dev_attr_id);
        device_create_file(&inst->class_dev, &dev_attr_name);
        device_create_file(&inst->class_dev, &dev_attr_extentions);
        device_create_file(&inst->class_dev, &dev_attr_regcnt);
#else
	class_device_create_file(&inst->class_dev, &class_device_attr_id);
	class_device_create_file(&inst->class_dev, &class_device_attr_name);
	class_device_create_file(&inst->class_dev, &class_device_attr_extentions);
	class_device_create_file(&inst->class_dev, &class_device_attr_regcnt);
#endif

#ifdef SYSFS_SUPPORT
	err = sysfs_create_group(&inst->class_dev.kobj, &pid_group);
	if (err)
		goto out_unreg;
	if (inst->obj)
		sysfs_create_link(&inst->class_dev.kobj, &inst->obj->class_dev.kobj, "obj");
	if (inst->st) {
		sprintf(name,"layer.%d", inst->id & LAYER_ID_MASK);
		sysfs_create_link(&inst->st->class_dev.kobj, &inst->class_dev.kobj, name);
		sysfs_create_link(&inst->class_dev.kobj, &inst->st->class_dev.kobj, "stack");
		if (inst->st->mgr == inst) {
			sysfs_create_link(&inst->st->class_dev.kobj, &inst->class_dev.kobj, "mgr");
		}
	}
#endif
	return(err);

#ifdef SYSFS_SUPPORT
out_unreg:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
	device_unregister(&inst->class_dev);
#else
	class_device_unregister(&inst->class_dev);
#endif
	return(err);
#endif
}

void
mISDN_unregister_sysfs_inst(mISDNinstance_t *inst)
{
	char	name[8];

	if (inst && inst->id) {
		if (inst->st) {
			sprintf(name,"layer.%d", inst->id & LAYER_ID_MASK);

#ifdef SYSFS_SUPPORT
			sysfs_remove_link(&inst->st->class_dev.kobj, name);
			sysfs_remove_link(&inst->class_dev.kobj, "stack");
			if (inst->st->mgr == inst)
				sysfs_remove_link(&inst->st->class_dev.kobj, "mgr");
#endif
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#ifdef SYSFS_SUPPORT
		device_unregister(&inst->class_dev);
#endif
#else
		class_device_unregister(&inst->class_dev);
#endif
	}
}

int
mISDN_sysfs_inst_init(void)
{
	return(class_register(&inst_dev_class));
}

void
mISDN_sysfs_inst_cleanup(void)
{
	class_unregister(&inst_dev_class);
}
