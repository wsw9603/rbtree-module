#define pr_fmt(fmt) KBUILD_MODNAME ":[func:%s,line:%d]" fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/rbtree.h>

#define PAT_NULL     "\e[0m"
#define PAT_BG_RED   "\e[45m"
#define PAT_BG_BLACK "\e[40m"
#define PAT_FG_RED   "\e[35m"
#define PAT_FG_BLACK "\e[30m"

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

	pr_info("find node (key:%d, addr:0x%llx)\n", 6, (unsigned long long)my_rb_search(&my_root, 6));
	pr_info("find node (key:%d, addr:0x%llx)\n", 9, (unsigned long long)my_rb_search(&my_root, 9));
	pr_info("find node (key:%d, addr:0x%llx)\n", 18, (unsigned long long)my_rb_search(&my_root, 18));

	return 0;
}
module_init(mytree_init);

static void __exit mytree_exit(void)
{
	pr_info("find node (key:%d, addr:0x%llx)\n", 6, (unsigned long long)my_rb_search(&my_root, 6));
	pr_info("find node (key:%d, addr:0x%llx)\n", 9, (unsigned long long)my_rb_search(&my_root, 9));
	pr_info("find node (key:%d, addr:0x%llx)\n", 18, (unsigned long long)my_rb_search(&my_root, 18));

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
