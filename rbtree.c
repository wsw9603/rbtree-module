#define pr_fmt(fmt) KBUILD_MODNAME ":[func:%s,line:%d]" fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/rbtree.h>
#include <linux/proc_fs.h>
#include <linux/rbtree_augmented.h>

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
	struct list_head list_head; // 用作层次遍历时的队列元素
	int no; // 节点在完全二叉树中的编号，根节点为0，左右子节点分别为2n+1,2n+2
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

static enum {
	order,
	color,
	nr_read_mod,
} read_mod = color;
#define KBUF_SIZE 4096

static size_t order_show(char *buf)
{
	int ret;
	size_t pos = 0;
	struct my_struct *data;
	struct rb_node *node = rb_first(&my_root);

	while (node) {
		data = rb_entry(node, struct my_struct, rb_node);
		ret = snprintf(buf + pos, KBUF_SIZE - pos, "node (key:%d, addr:0x%llx)\n", data->key, (unsigned long long)data);
		pos += ret;
		if (pos >= KBUF_SIZE) {
			pr_warn("buf overflow!!\n");
			break;
		}
		node = rb_next(node);
	}

	return pos;
}

/*
 * 获取红黑树的高度
 */
static int get_rb_hight(struct rb_root *root)
{
	int max_hight = 0, cur_hight = 0;
	struct rb_node *cur = root->rb_node, *parent;

	while (cur) {
		if (cur->rb_left) {
			cur = cur->rb_left;
			cur_hight++;
			max_hight = max_hight > cur_hight ?  max_hight : cur_hight;
			continue;
		}
		if (cur->rb_right) {
			cur = cur->rb_right;
			cur_hight++;
			max_hight = max_hight > cur_hight ?  max_hight : cur_hight;
			continue;
		}

		// pr_info("node %d\n", rb_entry(cur, struct my_struct, rb_node)->key);
		parent = rb_parent(cur);
		while (parent &&
		       (cur == parent->rb_right || !parent->rb_right)) {
			cur = parent;
			parent = rb_parent(cur);
			cur_hight--;
		}
		if (!parent)
			break;
		// pr_info("node %d\n", rb_entry(parent, struct my_struct, rb_node)->key);
		cur = parent->rb_right;
	}

	return max_hight;
}

/*
                 ______________xx______________
                /                              \
         ______xx______                  ______xx______
        /              \                /              \
     __xx__          __xx__          __xx__          __xx__
    /      \        /      \        /      \        /      \
   xx      xx      xx      xx      xx      xx      xx      xx
  /  \    /  \    /  \    /  \    /  \    /  \    /  \    /  \
 xx  xx  xx  xx  xx  xx  xx  xx  xx  xx  xx  xx  xx  xx  xx  xx

 将一棵红黑树以如上格式输出到屏幕上
 除叶子节点外，每层的组成部分为
     数字层:   <空格><下划线><数字><下划线><空格>
     斜杠层:<空格></><--------空格--------><\><空格>
     以叶子节点为第0层，则第n层各项的宽度为:
     数字层:   <2^n+1><2^n-2><2><2^n-2><2^n+1>
     斜杠层:  <2^n></><   2^(n+1)-2   ><\><2^n>
*/

static size_t
print_rb_key_level(struct my_struct *data, int hight, char *buf, size_t len)
{
	size_t pos = 0;
	int space_len = (1 << hight) + 1;
	int underscore_len = (1 << hight) - 2;

	if (data->rb_node.rb_left) {
		memset(buf, ' ', space_len);
		memset(buf + space_len, '_', underscore_len);
	} else
		memset(buf, ' ', space_len + underscore_len);
	pos += space_len + underscore_len;

	pos += snprintf(buf + pos, len - pos, "%s%2d" PAT_NULL,
			rb_is_red(&data->rb_node) ?
			PAT_BG_RED : PAT_BG_BLACK,
			data->key);

	if (data->rb_node.rb_right) {
		memset(buf + pos, '_', underscore_len);
		memset(buf + pos + underscore_len, ' ', space_len);
	} else
		memset(buf + pos, ' ', space_len + underscore_len);
	pos += space_len + underscore_len;

	return pos;
}

static size_t
print_rb_slash_level(struct my_struct *data, int hight, char *buf, size_t len)
{
	int ele_width = 1 << (hight + 2);

	memset(buf, ' ', ele_width);
	if (data->rb_node.rb_left)
		buf[1 << hight] = '/';
	if (data->rb_node.rb_right)
		buf[ele_width - (1 << hight) - 1] = '\\';

	return ele_width;
}

static size_t color_show(char *buf)
{
	size_t pos = 0;
	LIST_HEAD(h1);
	LIST_HEAD(h2);
	struct list_head *p1 = &h1, *p2 = &h2, *tmp;
	struct rb_node *node = my_root.rb_node;
	struct my_struct *n, *data_tmp;
	struct my_struct *data = rb_entry(node, struct my_struct, rb_node);

	int lvl = 0;
	int hight = get_rb_hight(&my_root);

	if (!node)
		return 0;
	list_add_tail(&data->list_head, p1);
	data->no = 0;

	while (!list_empty(p1)) {
		int start_no = (1 << lvl) - 1;
		int ele_width = 1 << (hight - lvl + 2);

		list_for_each_entry_safe(data, n, p1, list_head) {
			while (start_no < data->no) {
				memset(buf + pos, ' ', ele_width);
				start_no++;
				pos += ele_width;
			}

			pos += print_rb_key_level(data, hight - lvl, buf + pos,
						  KBUF_SIZE - pos);
			start_no++;
		}
		buf[pos++] = '\n';

		start_no = (1 << lvl) - 1;
		list_for_each_entry_safe(data, n, p1, list_head) {
			list_del(&data->list_head);

			while (start_no < data->no) {
				memset(buf + pos, ' ', ele_width);
				start_no++;
				pos += ele_width;
			}
			pos += print_rb_slash_level(data, hight - lvl, buf + pos,
						    KBUF_SIZE - pos);
			start_no++;

			if (data->rb_node.rb_left) {
				data_tmp = rb_entry(data->rb_node.rb_left, struct my_struct, rb_node);
				data_tmp->no = data->no * 2 + 1;
				list_add_tail(&data_tmp->list_head, p2);
			}

			if (data->rb_node.rb_right) {
				data_tmp = rb_entry(data->rb_node.rb_right, struct my_struct, rb_node);
				data_tmp->no = data->no * 2 + 2;
				list_add_tail(&data_tmp->list_head, p2);
			}
		}
		buf[pos++] = '\n';

		tmp = p1;
		p1 = p2;
		p2 = tmp;
		lvl++;
	}

	return pos;
}

static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	char *kbuf;
	ssize_t ret;
	size_t pos = 0;

	kbuf = kmalloc(KBUF_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	switch (read_mod) {
	case order:
		pos = order_show(kbuf);
		break;
	case color:
		pos = color_show(kbuf);
		break;
	default:
		BUG();
	}

	if (*off >= pos) {
		ret = 0;
		goto out;
	}

	if (pos - *off < len)
		len = pos - *off;
	if (copy_to_user(buf, kbuf + *off, len)) {
		pr_err("copy_to_user failed\n");
		ret = -EFAULT;
		goto out;
	}
	*off += len;
	ret = len;
out:
	kfree(kbuf);
	return ret;
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
		case 'c':
			if (key >= 0 && key < nr_read_mod) {
				read_mod = key;
				break;
			}
			/* fall through */
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
 * a 插入节点 add
 * d 删除节点 delete
 * s 显示节点 show
 * c 设置读文件的显示模式 config
 * 多条命令用分号隔开，依次执行，遇到出错的命令即停止并返回错误
 * eg:
 *    a12,3,5;d3;s12\n
 */

static ssize_t my_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	int ret;
	char *kbuf, *it, *end, *p;

	kbuf = kmalloc(KBUF_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, buf, len)) {
		ret = -EFAULT;
		goto out;
	}

	kbuf[len - 1] = '\0'; // 最后一个是回车符

	it = kbuf;
	end = kbuf + len - 1;
	while (it < end) {
		p = strchrnul(it, ';');
		*p = '\0';

		ret = parse_buf(it, p);
		if (ret < 0)
			goto out;

		it = p + 1;
	}

	ret = len;
out:
	kfree(kbuf);
	return ret;
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
//		pr_info("insert node (key:%d, addr:0x%llx)\n", tmp->key, (unsigned long long)tmp);

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
	struct rb_node *node;
	struct my_struct *tmp;

	proc_remove(my_proc_file);

	node = rb_first(&my_root);
	while (node) {
		tmp = rb_entry(node, struct my_struct, rb_node);

		rb_erase(node, &my_root);
//		pr_info("erase node (key:%d, addr:0x%llx)\n", tmp->key, (unsigned long long)tmp);

		kfree(tmp);

		node = rb_first(&my_root);
	}
}
module_exit(mytree_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wensheng <wsw9603@qq.com>");
