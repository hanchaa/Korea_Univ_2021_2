/*
 * @author: 차주한 김경하
 * 2021.10.16
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>

#define BUFFER_MAX_LEN 2000
#define PROC_DIRNAME "myproc"
#define PROC_FILENAME "myproc"

struct BLK_QUEUE {
	const char *fs_name;
	long sec;
	long usec;
	sector_t block_number;
};

extern struct BLK_QUEUE blk_queue[BUFFER_MAX_LEN];
extern int q_front;
extern int q_rear;

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_file;

// open proc_dir & proc_file
static int my_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Module open!!\n");

    return 0;
}

// user가 myproc 파일을 read 할 때 dequeue해서 write info 하나씩 출력
static ssize_t my_read(struct file *file, char __user *user_buffer, size_t count, loff_t *ppos) {
    char s[512] = "";
    int len = 0;

    if (q_front != q_rear) {
        q_front = (q_front + 1) % BUFFER_MAX_LEN;
        len = sprintf(s, "FS: %s || TIME: %ld.%ld || BLK_NUMBER: %lu\n", blk_queue[q_front].fs_name, blk_queue[q_front].sec, blk_queue[q_front].usec, blk_queue[q_front].block_number);
        copy_to_user(user_buffer, s, len);  // kernel에서 user로 copy
    }

    return len;
}

// user가 myproc 파일에 write 할 때 사용
static ssize_t my_write(struct file * file, const char __user *user_buffer, size_t count, loff_t *ppos){
    char kernel_buffer[100];
    copy_from_user(kernel_buffer, user_buffer, sizeof(kernel_buffer));  // user에서 kernel로 copy

    printk(KERN_INFO "Simple Module write!: %s\n", kernel_buffer);

    return count;
}

// 정의한 함수 연결
static const struct file_operations myproc_fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .read = my_read,
    .write = my_write,
};

// init할 때 myproc directory, file 생성
static int __init my_module_init(void) {
    printk(KERN_INFO "Hello My Module\n");
    proc_dir = proc_mkdir(PROC_DIRNAME, NULL);
    proc_file = proc_create(PROC_FILENAME, 0600, proc_dir, &myproc_fops);

    return 0;
}

// init에서 생성한 directory, file 삭제
static void __exit my_module_exit(void) {
    printk(KERN_INFO "Bye My Module\n");
    remove_proc_entry(PROC_FILENAME, proc_dir);
    remove_proc_entry(PROC_DIRNAME, NULL);

    return;
}



module_init(my_module_init);
module_exit(my_module_exit);

MODULE_AUTHOR("Juhan Cha, Kyungha Kim");
MODULE_DESCRIPTION("Log block i/o");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
