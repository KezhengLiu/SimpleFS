// tinyfs.c	
#include &lt;linux/init.h&gt;	
#include &lt;linux/module.h&gt;	
#include &lt;linux/fs.h&gt;	
#include &lt;linux/uaccess.h&gt;	
#include "tinyfs.h"	
struct file_blk block[MAX_FILES+1];	
int curr_count = 0; // 我勒个去，竟然使用了全局变量！	
// 获得一个尚未使用的文件块，保存新创建的文件或者目录	
static int get_block(void)	
{	
    int i;	
    // 就是一个遍历，但实现快速。	
    for (i = 2; i &lt; MAX_FILES; i++) {	
        if (!block[i].busy) {	
            block[i].busy = 1;	
            return i;	
        }	
    }	
    return -1;	
}	
static struct inode_operations tinyfs_inode_ops;	
// 读取目录的实现	
static int tinyfs_readdir(struct file *filp, void *dirent, filldir_t filldir)	
{	
    loff_t pos;	
    struct file_blk *blk;	
    struct dir_entry *entry;	
    int i;	
    pos = filp-&gt;f_pos;	
    if (pos)	
        return 0;	
    blk = (struct file_blk *)filp-&gt;f_dentry-&gt;d_inode-&gt;i_private;	
    if (!S_ISDIR(blk-&gt;mode)) {	
        return -ENOTDIR;	
    }	
    // 循环获取一个目录的所有文件的文件名	
    entry = (struct dir_entry *)&amp;blk-&gt;data[0];	
    for (i = 0; i &lt; blk-&gt;dir_children; i++) {	
        filldir(dirent, entry[i].filename, MAXLEN, pos, entry[i].idx, DT_UNKNOWN);	
        filp-&gt;f_pos += sizeof(struct dir_entry);	
        pos += sizeof(struct dir_entry);	
    }	
    return 0;	
}	
// read实现	
ssize_t tinyfs_read(struct file * filp, char __user * buf, size_t len, loff_t *ppos)	
{	
    struct file_blk *blk;	
    char *buffer;	
    blk = (struct file_blk *)filp-&gt;f_path.dentry-&gt;d_inode-&gt;i_private;	
    if (*ppos &gt;= blk-&gt;file_size)	
        return 0;	
    buffer = (char *)&amp;blk-&gt;data[0];	
    len = min((size_t) blk-&gt;file_size, len);	
    if (copy_to_user(buf, buffer, len)) {	
        return -EFAULT;	
    }	
    *ppos += len;	
    return len;	
}	
// write实现	
ssize_t tinyfs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos)	
{	
    struct file_blk *blk;	
    char *buffer;	
    blk = filp-&gt;f_path.dentry-&gt;d_inode-&gt;i_private;	
    buffer = (char *)&amp;blk-&gt;data[0];	
    buffer += *ppos;	
    if (copy_from_user(buffer, buf, len)) {	
        return -EFAULT;	
    }	
    *ppos += len;	
    blk-&gt;file_size = *ppos;	
    return len;	
}	
const struct file_operations tinyfs_file_operations = {	
    .read = tinyfs_read,	
    .write = tinyfs_write,	
};	
const struct file_operations tinyfs_dir_operations = {	
    .owner = THIS_MODULE,	
    .readdir = tinyfs_readdir,	
};	
// 创建文件的实现	
static int tinyfs_do_create(struct inode *dir, struct dentry *dentry, umode_t mode)	
{	
    struct inode *inode;	
    struct super_block *sb;	
    struct dir_entry *entry;	
    struct file_blk *blk, *pblk;	
    int idx;	
    sb = dir-&gt;i_sb;	
    if (curr_count &gt;= MAX_FILES) {	
        return -ENOSPC;	
    }	
    if (!S_ISDIR(mode) &amp;&amp; !S_ISREG(mode)) {	
        return -EINVAL;	
    }	
    inode = new_inode(sb);	
    if (!inode) {	
        return -ENOMEM;	
    }	
    inode-&gt;i_sb = sb;	
    inode-&gt;i_op = &amp;tinyfs_inode_ops;	
    inode-&gt;i_atime = inode-&gt;i_mtime = inode-&gt;i_ctime = CURRENT_TIME;	
    idx = get_block(); // 获取一个空闲的文件块保存新文件	
    blk = &amp;block[idx];	
    inode-&gt;i_ino = idx;	
    blk-&gt;mode = mode;	
    curr_count ++;	
    if (S_ISDIR(mode)) {	
        blk-&gt;dir_children = 0;	
        inode-&gt;i_fop = &amp;tinyfs_dir_operations;	
    } else if (S_ISREG(mode)) {	
        blk-&gt;file_size = 0;	
        inode-&gt;i_fop = &amp;tinyfs_file_operations;	
    }	
    inode-&gt;i_private = blk;	
    pblk = (struct file_blk *)dir-&gt;i_private;	
    entry = (struct dir_entry *)&amp;pblk-&gt;data[0];	
    entry += pblk-&gt;dir_children;	
    pblk-&gt;dir_children ++;	
    entry-&gt;idx = idx;	
    strcpy(entry-&gt;filename, dentry-&gt;d_name.name);	
    // VFS穿针引线的关键步骤，将VFS的inode链接到链表	
    inode_init_owner(inode, dir, mode); 	
    d_add(dentry, inode);	
    return 0;	
}	
static int tinyfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)	
{	
    return tinyfs_do_create(dir, dentry, S_IFDIR | mode);	
}	
static int tinyfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)	
{	
    return tinyfs_do_create(dir, dentry, mode);	
}	
static struct inode *tinyfs_iget(struct super_block *sb, int idx)	
{	
    struct inode *inode;	
    struct file_blk *blk;	
    inode = new_inode(sb);	
    inode-&gt;i_ino = idx;	
    inode-&gt;i_sb = sb;	
    inode-&gt;i_op = &amp;tinyfs_inode_ops;	
    blk = &amp;block[idx];	
    if (S_ISDIR(blk-&gt;mode))	
        inode-&gt;i_fop = &amp;tinyfs_dir_operations;	
    else if (S_ISREG(blk-&gt;mode))	
        inode-&gt;i_fop = &amp;tinyfs_file_operations;	
    inode-&gt;i_atime = inode-&gt;i_mtime = inode-&gt;i_ctime = CURRENT_TIME;	
    inode-&gt;i_private = blk;	
    return inode;	
}	
struct dentry *tinyfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags)	
{	
    struct super_block *sb = parent_inode-&gt;i_sb;	
    struct file_blk *blk;	
    struct dir_entry *entry;	
    int i;	
    blk = (struct file_blk *)parent_inode-&gt;i_private;	
    entry = (struct dir_entry *)&amp;blk-&gt;data[0];	
    for (i = 0; i &lt; blk-&gt;dir_children; i++) {	
        if (!strcmp(entry[i].filename, child_dentry-&gt;d_name.name)) {	
            struct inode *inode = tinyfs_iget(sb, entry[i].idx);	
            struct file_blk *inner = (struct file_blk *)inode-&gt;i_private;	
            inode_init_owner(inode, parent_inode, inner-&gt;mode);	
            d_add(child_dentry, inode);	
            return NULL;	
        }	
    }	
    return NULL;	
}	
int tinyfs_rmdir(struct inode *dir, struct dentry *dentry)	
{	
    struct inode *inode = dentry-&gt;d_inode;	
    struct file_blk *blk = (struct file_blk *)inode-&gt;i_private;	
    blk-&gt;busy = 0;	
    return simple_rmdir(dir, dentry);	
}	
int tinyfs_unlink(struct inode *dir, struct dentry *dentry)	
{	
    int i;	
    struct inode *inode = dentry-&gt;d_inode;	
    struct file_blk *blk = (struct file_blk *)inode-&gt;i_private;	
    struct file_blk *pblk = (struct file_blk *)dir-&gt;i_private;	
    struct dir_entry *entry;	
    // 更新其上层目录	
    entry = (struct dir_entry *)&amp;pblk-&gt;data[0];	
    for (i = 0; i &lt; pblk-&gt;dir_children; i++) {	
        if (!strcmp(entry[i].filename, dentry-&gt;d_name.name)) {	
            int j;	
            for (j = i; j &lt; pblk-&gt;dir_children - 1; j++) {	
                memcpy(&amp;entry[j], &amp;entry[j+1], sizeof(struct dir_entry));	
            }	
            pblk-&gt;dir_children --;	
            break;	
        }	
    }	
    blk-&gt;busy = 0;	
    return simple_unlink(dir, dentry);	
}	
static struct inode_operations tinyfs_inode_ops = {	
    .create = tinyfs_create,	
    .lookup = tinyfs_lookup,	
    .mkdir = tinyfs_mkdir,	
    .rmdir = tinyfs_rmdir,	
    .unlink = tinyfs_unlink,	
};	
int tinyfs_fill_super(struct super_block *sb, void *data, int silent)	
{	
    struct inode *root_inode;	
    int mode = S_IFDIR;	
    root_inode = new_inode(sb);	
    root_inode-&gt;i_ino = 1;	
    inode_init_owner(root_inode, NULL, mode);	
    root_inode-&gt;i_sb = sb;	
    root_inode-&gt;i_op = &amp;tinyfs_inode_ops;	
    root_inode-&gt;i_fop = &amp;tinyfs_dir_operations;	
    root_inode-&gt;i_atime = root_inode-&gt;i_mtime = root_inode-&gt;i_ctime = CURRENT_TIME;	
    block[1].mode = mode;	
    block[1].dir_children = 0;	
    block[1].idx = 1;	
    block[1].busy = 1;	
    root_inode-&gt;i_private = &amp;block[1];	
    sb-&gt;s_root = d_make_root(root_inode);	
    curr_count ++;	
    return 0;	
}	
static struct dentry *tinyfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)	
{	
    return mount_nodev(fs_type, flags, data, tinyfs_fill_super);	
}	
static void tinyfs_kill_superblock(struct super_block *sb)	
{	
    kill_anon_super(sb);	
}	
struct file_system_type tinyfs_fs_type = {	
    .owner = THIS_MODULE,	
    .name = "tinyfs",	
    .mount = tinyfs_mount,	
    .kill_sb = tinyfs_kill_superblock,	
};	
static int tinyfs_init(void)	
{	
    int ret;	
    memset(block, 0, sizeof(block));	
    ret = register_filesystem(&amp;tinyfs_fs_type);	
    if (ret)	
        printk("register tinyfs failed\n");	
    return ret;	
}	
static void tinyfs_exit(void)	
{	
    unregister_filesystem(&amp;tinyfs_fs_type);	
}	


module_init(tinyfs_init);	
module_exit(tinyfs_exit);	
MODULE_LICENSE("GPL");