#include "init/initcall.h"
#include "init/param.h"
#include "common/list.h"
#include "sync/spinlock.h"
#include "mm/cache.h"
#include "sched/sched.h"
#include "sched/ktaskd.h"
#include "fs/vfs.h"
#include "fs/type/devfs.h"
#include "log/log.h"

typedef enum device_type {
    CHAR_DEV,
    BLCK_DEV,
} device_type_t;

typedef struct devfs_device {
    list_head_t list;

    char *name;

    dentry_t *dentry;

    device_type_t type;
    union {
        void *dev;
        block_device_t *blockdev;
        char_device_t *chardev;
    };
} devfs_device_t;

static fs_t *devfs;
static thread_t *devfs_task;
static DEFINE_LIST(devfs_devices);
static DEFINE_LIST(devfs_pending);
static DEFINE_SPINLOCK(devfs_lock);
static const char *mntpoint = "/dev";

static void char_file_open(file_t *file, inode_t *inode) {
    devfs_device_t *device = inode->private;
    file->private = device;
}

static off_t char_file_seek(file_t *file, off_t off) {
    return 0;
}

static ssize_t char_file_read(file_t *file, char *buff, size_t bytes) {
    devfs_device_t *device = file->private;
    char_device_t *cdev = device->chardev;

    return cdev->ops->read(cdev, buff, bytes);
}

static ssize_t char_file_write(file_t *file, const char *buff, size_t bytes) {
    devfs_device_t *device = file->private;
    char_device_t *cdev = device->chardev;

    return cdev->ops->write(cdev, buff, bytes);
}

static void char_file_close(file_t *file) {
}

static file_ops_t char_file_ops = {
    .open = char_file_open,
    .close = char_file_close,
    .seek = char_file_seek,
    .read = char_file_read,
    .write = char_file_write,
};

static inode_ops_t char_inode_ops = {
    .file_ops = &char_file_ops,
};

static void block_file_open(file_t *file, inode_t *inode) {
    devfs_device_t *device = inode->private;
    file->private = device;
}

static off_t block_file_seek(file_t *file, off_t off) {
    return 0;
}

static ssize_t block_file_read(file_t *file, char *buff, size_t bytes) {
    //TODO implement buffered IO scheduler for block abstracted reads

    return -1;
}

static ssize_t block_file_write(file_t *file, const char *buff, size_t bytes) {
    //TODO implement buffered IO scheduler for block abstracted writes

    return -1;
}

static void block_file_close(file_t *file) {
}

static file_ops_t block_file_ops = {
    .open = block_file_open,
    .close = block_file_close,
    .seek = block_file_seek,
    .read = block_file_read,
    .write = block_file_write,
};

static inode_ops_t block_inode_ops = {
    .file_ops = &block_file_ops,
};

static void root_inode_lookup(inode_t *inode, dentry_t *target) {
}

static inode_ops_t root_inode_ops = {
    .lookup = root_inode_lookup,
};

static inode_t * devfs_inode_alloc(devfs_device_t *device) {
    void *ops = NULL;

    switch(device->type) {
        case CHAR_DEV:
            ops = &char_inode_ops;
            break;
        case BLCK_DEV:
            ops = &block_inode_ops;
            break;
    }

    inode_t *inode = inode_alloc(devfs, ops);
    inode->private = device;

    return inode;
}

static dentry_t * devfs_create(fs_type_t *fs_type, const char *device);

static fs_type_t devfs_type = {
    .name  = "devfs",
    .flags = FSTYPE_FLAG_NODEV,
    .create  = devfs_create,
};

static void devfs_fill(fs_t *fs) {
    fs->root = dentry_alloc("");
    fs->root->fs = fs;
    fs->root->inode = inode_alloc(fs, &root_inode_ops);
    fs->root->inode->flags |= INODE_FLAG_DIRECTORY;
}

static dentry_t * devfs_create(fs_type_t *fs_type, const char *device) {
    return fs_create_single(fs_type, devfs_fill);
}

static void devfs_add_dev(void *device, device_type_t type, char *name) {
    devfs_device_t *d = kmalloc(sizeof(devfs_device_t));
    d->name = name;
    d->dentry = dentry_alloc(d->name);

    d->type = type;
    d->dev = device;

    uint32_t flags;
    spin_lock_irqsave(&devfs_lock, &flags);
    list_add_before(&d->list, &devfs_pending);

    if(devfs_task) thread_wake(devfs_task);

    spin_unlock_irqstore(&devfs_lock, flags);
}

static char * get_strpath(const char *name) {
    uint32_t mntlen = strlen(mntpoint);
    uint32_t namelen = strlen(name);

    char *str = kmalloc(mntlen + namelen + 2);
    memcpy(str, mntpoint, mntlen);
    memcpy(str + mntlen, "/", 1);
    memcpy(str + mntlen + 1, name, namelen);
    str[mntlen + namelen + 1] = '\0';

    return str;
}

bool devfs_lookup(const char *name, path_t *out) {
    char *path = get_strpath(name);
    bool ret = vfs_lookup(NULL, path, out);
    kfree(path);
    return ret;
}

void devfs_add_chardev(char_device_t *device, char *name) {
    devfs_add_dev(device, CHAR_DEV, name);
}

void devfs_add_blockdev(block_device_t *device, char *name) {
    devfs_add_dev(device, BLCK_DEV, name);
}

static bool devfs_set_mntpoint(char *point) {
    mntpoint = point;

    return true;
}

cmdline_param("devfs.mount", devfs_set_mntpoint);

static bool create_path(path_t *start, const char *orig_path) {
    char *path = strdup(orig_path);
    char *part = path;

    while(part && *part) {
        part = strchr(part, '/');

        if(part) part[0] = '\0';
        if(!vfs_mkdir(start, path, 0755)) return false;
        if(part) {
            part[0] = '/';
            part++;
        }
    }

    kfree(path);

    return true;
}

//called with devfs_lock held
static bool devfs_update() {
    if(list_empty(&devfs_pending)) {
        return false;
    }

    devfs_device_t *d = list_first(&devfs_pending, devfs_device_t, list);
    list_rm(&d->list);

    d->dentry->inode = devfs_inode_alloc(d);
    dentry_activate(d->dentry, devfs->root);

    list_add(&d->list, &devfs_devices);
    kprintf("devfs - added \"%s/%s\"", mntpoint, d->dentry->name);

    return true;
}

//called with devfs_lock held
void devfs_publish_pending() {
    while(devfs_update());
}

static void devfs_run(void *UNUSED(arg)) {
    irqenable();

    while(true) {
        spin_lock_irq(&devfs_lock);

        devfs_publish_pending();
        thread_sleep_prepare();

        spin_unlock_irq(&devfs_lock);

        sched_switch();
    }
}

static INITCALL devfs_init() {
    register_fs_type(&devfs_type);

    return 0;
}

static INITCALL devfs_mount() {
    devfs = vfs_fs_create("devfs", NULL);

    devfs_publish_pending();

    if(mntpoint) {
        path_t wd, target;
        wd.mount = root_mount;
        wd.dentry = root_mount->fs->root;

        if(!create_path(&wd, mntpoint)) {
            kprintf("devfs - could not create path \"%s\"", mntpoint);
        } else if(!vfs_lookup(&wd, mntpoint, &target)) {
            kprintf("devfs - could not lookup \"%s\"", mntpoint);
        } else if(vfs_do_mount(devfs, &target)) {
            kprintf("devfs - mounted at \"%s\"", mntpoint);
        } else {
            kprintf("devfs - could not mount at \"%s\"", mntpoint);
        }
    }

    ktaskd_request("kdevfsd", devfs_run, NULL);

    return 0;
}

core_initcall(devfs_init);
fs_initcall(devfs_mount);
