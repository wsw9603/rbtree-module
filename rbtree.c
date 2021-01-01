#define pr_fmt(fmt) KBUILD_MODNAME ":[func:%s,line:%d]" fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/rbtree.h>
#include <linux/proc_fs.h>

#define PAT_NULL     "\e[0m"
#define PAT_BG_RED   "\e[45m"
#define PAT_BG_BLACK "\e[40m"
#define PAT_FG_RED   "\e[35m"
#define PAT_FG_BLACK "\e[30m"

static struct proc_dir_entry *my_proc_file;
static struct rb_root my_root = RB_ROOT;

struct my_struct {
	int key;
	struct rb_node rb_node;
};

static struct my_struct *my_rb_insert(struct rb_root *root, struct my_struct *data)
{
	struct rb_node **link = &root->rb_node;
	struct rb_node *parent = NULL; // 这个必须要初始化成NULL，在空树中插入节点的时候，其父节点应为空
	struct my_struct *tmp;

	while (*link) {
		parent = *link;
		tmp = rb_entry(*link, struct my_struct, rb_node);

		if (data->key < tmp->key)
			link = &parent->rb_left;
		else if (data->key > tmp->key)
			link = &parent->rb_right;
		else
			return NULL;
	}

	rb_link_node(&data->rb_node, parent, link);
	rb_insert_color(&data->rb_node, root);

	return data;
}

static struct my_struct *my_rb_search(struct rb_root *root, int key)
{
	struct my_struct *data;
	struct rb_node *node = root->rb_node;

	while (node) {
		data = rb_entry(node, struct my_struct, rb_node);
		if (key < data->key)
			node = node->rb_left;
		else if (key > data->key)
			node = node->rb_right;
		else
			return data;
	}

	return NULL;
}

static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	char kbuf[4096];
	size_t kbuf_len = 4096, pos = 0, ret;
	struct rb_node *node = rb_first(&my_root);
	struct my_struct *data;

	while (node) {
		data = rb_entry(node, struct my_struct, rb_node);
		ret = snprintf(kbuf + pos, kbuf_len - pos, "node (key:%d, addr:0x%llx)\n", data->key, (unsigned long long)data);
		pos += ret;
		if (pos >= kbuf_len)
			break;
		node = rb_next(node);
	}

	if (*off >= pos)
		return 0;

	if (pos - *off < len)
		len = pos - *off;
	if (copy_to_user(buf, kbuf + *off, len)) {
		pr_err("copy_to_user failed\n");
		return -EFAULT;
	}
	*off += len;
	return len;
}

static int parse_buf(char *buf, char *end)
{
	int ret;
	char cmd = *(buf++), *p;
	long long key;
	struct my_struct *data;

	while (buf < end) {
		p = strchrnul(buf, ',');
		*p = '\0';
		ret = kstrtoll(buf, 0, &key);
		if (ret < 0) {
			pr_err("invalid input");
			return ret;
		}

		switch (cmd) {
		case 'a':
			data = kmalloc(sizeof(*data), GFP_KERNEL);
			if (!data)
				return -ENOMEM;
			data->key = (int)key;
			if (!my_rb_insert(&my_root, data)) {
				pr_err("node of key %d exist\n", (int)key);
				kfree(data);
				return -EEXIST;
			}
			break;
		case 'd':
			data = my_rb_search(&my_root, (int)key);
			if (!data) {
				pr_err("node of key %d doesn't exist\n",
					(int)key);
				return -EEXIST;
			}
			rb_erase(&data->rb_node, &my_root);
			break;
		case 's':
			data = my_rb_search(&my_root, (int)key);
			if (!data) {
				pr_err("node of key %d doesn't exist\n",
					(int)key);
				return -EEXIST;
			}
			pr_info("node (key:%d, addr:0x%llx)\n",
				data->key, (unsigned long long)data);
			break;
		default:
			pr_err("invalid input");
			return -EINVAL;
		}

		buf = p + 1;
	}

	return 0;
}

/*
 * 写入格式:
 * <操作命令>[操作数][,操作数][;<操作命令>[操作数][,操作数]][...]
 * 支持的操作命令：
 * a 插入节点
 * d 删除节点
 * s 显示节点
 * 多条命令用分号隔开，依次执行，遇到出错的命令即停止并返回错误
 * eg:
 *    a12,3,5;d3;s12\n
 */

static ssize_t my_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	int ret;
	char kbuf[4096];
	char *it = kbuf, *end = it + len - 1, *p;

	if (copy_from_user(kbuf, buf, len))
		return -EFAULT;

	kbuf[len - 1] = '\0'; // 最后一个是回车符

	while (it < end) {
		p = strchrnul(it, ';');
		*p = '\0';

		ret = parse_buf(it, p);
		if (ret < 0)
			return ret;

		it = p + 1;
	}

	return len;
}

struct proc_ops mytree_proc_ops = {
	.proc_read = my_read,
	.proc_write = my_write,
};

static int __init mytree_init(void)
{
	int i;

	for (i = 0; i < 15; i++) {
		struct my_struct *tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
		if (!tmp)
			return 0;

		tmp->key = i;
		pr_info("insert node (key:%d, addr:0x%llx)\n", tmp->key, (unsigned long long)tmp);

		my_rb_insert(&my_root, tmp);
	}

	my_proc_file = proc_create("rbtree", 0644, NULL, &mytree_proc_ops);
	if (!my_proc_file)
		pr_warn("create proc file failed\n");

	return 0;
}
module_init(mytree_init);

static void __exit mytree_exit(void)
{
	pr_info("find node (key:%d, addr:0x%llx)\n", 6, (unsigned long long)my_rb_search(&my_root, 6));
	pr_info("find node (key:%d, addr:0x%llx)\n", 9, (unsigned long long)my_rb_search(&my_root, 9));
	pr_info("find node (key:%d, addr:0x%llx)\n", 18, (unsigned long long)my_rb_search(&my_root, 18));

	proc_remove(my_proc_file);

	struct rb_node *node = rb_first(&my_root);
	while (node) {
		struct my_struct *tmp = rb_entry(node, struct my_struct, rb_node);

		rb_erase(node, &my_root);
		pr_info("erase node (key:%d, addr:0x%llx)\n", tmp->key, (unsigned long long)tmp);

		kfree(tmp);

		node = rb_first(&my_root);
	}
}
module_exit(mytree_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wensheng <wsw9603@qq.com>");
