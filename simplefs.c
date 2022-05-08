#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/cred.h>
#include <linux/mount.h>
//每个文件系统需要一个MAGIC number
#define AUFS_MAGIC  0x64668735
//aufs文件系统的挂载点
static struct vfsmount *aufs_mount;
//根据创建的aufs文件系统的 super_block创建具体的inode结构体
static struct inode *aufs_get_inode(struct super_block *sb, int mode,
                dev_t dev)
{
    struct inode *inode = new_inode(sb);
 
    if (inode) {
        inode->i_mode = mode;
        inode->i_uid = current_fsuid();
        inode->i_gid = current_fsgid();
        inode->i_blocks = 0;
        inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
        switch (mode & S_IFMT) {
            default:
                init_special_inode(inode, mode, dev);
                break;
            case S_IFREG:
                printk("create a file \n");
                break;
            case S_IFDIR:
                inode->i_op = &simple_dir_inode_operations;
                inode->i_fop = &simple_dir_operations;
                printk("creat a dir file \n");
                 
                inode->__i_nlink++;
                break;
        }
    }
 
    return inode;
}
//把创建的inode和dentry结构体连接起来
static int aufs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
    struct inode * inode;
    int error = -EPERM;
 
    if (dentry->d_inode)
        return -EEXIST;
    inode = aufs_get_inode(dir->i_sb, mode, dev);
    if (inode) {
        d_instantiate(dentry, inode);
        dget(dentry);
        error = 0;
    }
     
    return error;
}
 
static int aufs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
    int res;
 
    res = aufs_mknod(dir, dentry, mode | S_IFDIR, 0);
    if (!res) {
        dir->__i_nlink++;
    }
 
    return res;
}
 
static int aufs_create(struct inode *dir, struct dentry *dentry, int mode)
{
    return aufs_mknod(dir, dentry, mode | S_IFREG, 0);
}
//根据父dentry、mode、name创建子dentry
static int aufs_create_by_name(const char *name, mode_t mode,
        struct dentry *parent, struct dentry **dentry)
{
    int error = 0;
 
    if (!parent) {
        if (aufs_mount && aufs_mount->mnt_sb) {
            parent = aufs_mount->mnt_sb->s_root;
        }
    }
 
    if (!parent) {
        printk("Ah! can not find a parent!\n");
        return -EFAULT;
    }
 
    *dentry = NULL;
    spin_lock(&parent->d_inode->i_lock);
    *dentry = lookup_one_len(name, parent, strlen(name));
    if (!IS_ERR(dentry)) {
        if ((mode & S_IFMT) == S_IFDIR)
            error = aufs_mkdir(parent->d_inode, *dentry, mode);
        else
            error = aufs_create(parent->d_inode, *dentry, mode);
    } else
        error = PTR_ERR(dentry);
    spin_unlock(&parent->d_inode->i_lock);
 
    return error;
}
//在aufs文件系统中创建文件
struct dentry *aufs_create_file(const char *name, mode_t mode,
            struct dentry *parent, void *data,
            struct file_operations *fops)
{
    struct dentry *dentry = NULL;
    int error;
 
    printk("aufs: creating file '%s'", name);
     
    error = aufs_create_by_name(name, mode, parent, &dentry);
    if (error) {
        dentry = NULL;
        goto exit;
    }
 
    if (dentry->d_inode) {
        if (data)
            dentry->d_inode->i_private = data;
        if (fops)
            dentry->d_inode->i_fop = fops;
    }
exit:
    return dentry;
}
//在aufs文件系统中创建一个文件夹
struct dentry *aufs_create_dir(const char *name, struct dentry *parent)
{
    return aufs_create_file(name, S_IFDIR | S_IRWXU | S_IRUGO, parent, NULL, NULL);
}
 
static int enabled = 1;
//对应于打开的aufs文件的读取方法
static ssize_t aufs_file_read(struct file *fle, char __user *buf, size_t nbytes, loff_t *ppos)
{
    char *s = enabled ? "aufs read enabled\n" : "aufs read disabled\n";
    dump_stack();
    return simple_read_from_buffer(buf, nbytes, ppos, s, strlen(s));
}
//对应于打开的aufs文件的写入方法
static ssize_t aufs_file_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    int res = *buffer - '0';
 
    if (res)
        enabled = 1;
    else
        enabled = 0;
 
    return count;
}
//对应具体打开文件的文件操作方式
static struct file_operations aufs_file_operations = {
    .read = aufs_file_read,
    .write = aufs_file_write,
};
//用于填充aufs的super_block
static int aufs_fill_super(struct super_block *sb, void *data, int silent)
{
    static struct tree_descr debug_files[] = {{""}};
 
    return simple_fill_super(sb, AUFS_MAGIC, debug_files);
}
//创建aufs文件系统的对应的根目录的dentry
static struct dentry *aufs_get_sb(struct file_system_type *fs_type,
        int flags, const char *dev_name, void *data)
{
    return mount_single(fs_type, flags, data, aufs_fill_super);
}
//初始化aufs文件系统的 file_system_type结构，每个文件系统对应一个这样的结构体，主要用于提供具体文件系统的//的信息，以及操作的方法
static struct file_system_type aufs_type = {
    .name = "aufs",
    .mount = aufs_get_sb,
    .kill_sb = kill_litter_super,
};
//创建aufs文件系统，同时创建对应的文件夹和文件
static int __init aufs_init(void)
{
    int ret;
 
    struct dentry *pslot;
     
    ret = register_filesystem(&aufs_type);
    if (ret) {
        printk(KERN_ERR "aufs: cannot register file system\n");
        return ret;
    }
 
    aufs_mount = kern_mount(&aufs_type);
    if (IS_ERR(aufs_mount)) {
        printk(KERN_ERR "aufs: cannot mount file system\n");
        unregister_filesystem(&aufs_type);
        return ret;
    }
 
    pslot = aufs_create_dir("woman_star", NULL); //创建woman_star文件系统，返回所创建文件夹的dentry
    aufs_create_file("lbb", S_IFREG | S_IRUGO, pslot, NULL, &aufs_file_operations);//在对应的文件夹下，创建具体的文件
    aufs_create_file("fbb", S_IFREG | S_IRUGO, pslot, NULL, &aufs_file_operations);
    aufs_create_file("lj1", S_IFREG | S_IRUGO, pslot, NULL, &aufs_file_operations);
 
    pslot = aufs_create_dir("man_star", NULL);
    aufs_create_file("ldh", S_IFREG | S_IRUGO, pslot, NULL, &aufs_file_operations);
    aufs_create_file("lcw", S_IFREG | S_IRUGO, pslot, NULL, &aufs_file_operations);
    aufs_create_file("jw",  S_IFREG | S_IRUGO, pslot, NULL, &aufs_file_operations);
 
    return 0;
}
//卸载aufs文件系统
static void __exit aufs_exit(void)
{
    kern_unmount(aufs_mount);
    unregister_filesystem(&aufs_type);
    aufs_mount = NULL;
}
 
module_init(aufs_init);
module_exit(aufs_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("This is a simple module");
MODULE_VERSION("Ver 0.1");