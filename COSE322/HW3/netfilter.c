/*
 * @author: 차주한 김경하
 * 2021.11.20
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <linux/tcp.h>

#define PROC_DIRNAME "my_netfilter"
#define PROC_FILENAME "rule"

#define NIPQUAD(addr) \
	((unsigned char *)&addr)[0], \
	((unsigned char *)&addr)[1], \
	((unsigned char *)&addr)[2], \
	((unsigned char *)&addr)[3]

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_file;

enum rule_type { FORWARDING, DROP };

typedef struct RULE {
    struct RULE *next;
    int src_port;
    int change_port;
    enum rule_type type;
} Rule;

Rule *rule_head = NULL;
Rule *rule_head_for_proc_read = NULL;

Rule *findRule(int src_port){
    Rule *cur = rule_head;

    while(cur != NULL){
        if (cur->src_port == src_port){
            return cur;
        }

        cur = cur->next;
    }

    return NULL;
}

static unsigned int netfilter_pre_routing(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {
    //NF_INET_PRE_ROUTING

    struct iphdr *iph = ip_hdr(skb);
    struct tcphdr *tcph = tcp_hdr(skb);
    Rule *rule;

    int src_port = ntohs(tcph->source);
    int dst_port = ntohs(tcph->dest);

    rule = findRule(src_port);

    if (rule != NULL) {
        if (rule->type == DROP) {
            printk(KERN_INFO "drop: PRE_ROUTING (%d; %d; %d; %d.%d.%d.%d; %d.%d.%d.%d)\n",
                iph->protocol,
                src_port,
                dst_port,
                NIPQUAD(iph->saddr),
                NIPQUAD(iph->daddr));

            tcph->source = tcph->dest = htons(rule->change_port);

            printk(KERN_INFO "drop: PRE_ROUTING (%d; %d; %d; %d.%d.%d.%d; %d.%d.%d.%d)\n",
                iph->protocol,
                ntohs(tcph->source),
                ntohs(tcph->dest),
                NIPQUAD(iph->saddr),
                NIPQUAD(iph->daddr));

            return NF_DROP;
        }

        if (rule->type == FORWARDING) {
            unsigned char dst_addr[4] = { 100, 1, 1, 10 };

            printk(KERN_INFO "forward: PRE_ROUTING (%d; %d; %d; %d.%d.%d.%d; %d.%d.%d.%d)\n",
                iph->protocol,
                src_port,
                dst_port,
                NIPQUAD(iph->saddr),
                NIPQUAD(iph->daddr));

            tcph->source = tcph->dest = htons(rule->change_port);
            iph->daddr = *(unsigned int *)dst_addr;

            return NF_ACCEPT;
        }
    }

    return NF_ACCEPT;
}

static struct nf_hook_ops netfilter_pre_routing_ops = {
    .hook = netfilter_pre_routing,
    .pf = PF_INET,
    .hooknum = NF_INET_PRE_ROUTING,
    .priority = 1,
};

static unsigned int netfilter_monitor_packet(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {
    //NF_INET_LOCAL_IN
    //NF_INET_FORWARD
    //NF_INET_POST_ROUTING

    struct iphdr *iph = ip_hdr(skb);
    struct tcphdr *tcph = tcp_hdr(skb);

    int src_port = ntohs(tcph->source);
    int dst_port = ntohs(tcph->dest);

    if (state->hook == NF_INET_LOCAL_IN && src_port == 3333 && dst_port == 3333)
        printk(KERN_INFO "drop: LOCAL_IN (%d; %d; %d; %d.%d.%d.%d; %d.%d.%d.%d)\n",
                iph->protocol,
                src_port,
                dst_port,
                NIPQUAD(iph->saddr),
                NIPQUAD(iph->daddr));

    if (state->hook == NF_INET_FORWARD && src_port == 7777 && dst_port == 7777)
        printk(KERN_INFO "forward: FORWARD (%d; %d; %d; %d.%d.%d.%d; %d.%d.%d.%d)\n",
            iph->protocol,
            src_port,
            dst_port,
            NIPQUAD(iph->saddr),
            NIPQUAD(iph->daddr));
    
    else if (state->hook == NF_INET_POST_ROUTING && src_port == 7777 && dst_port == 7777)
        printk(KERN_INFO "forward: POST_ROUTING (%d; %d; %d; %d.%d.%d.%d; %d.%d.%d.%d)\n",
        iph->protocol,
        src_port,
        dst_port,
        NIPQUAD(iph->saddr),
        NIPQUAD(iph->daddr));

    return NF_ACCEPT;
}

static struct nf_hook_ops netfilter_local_in_ops = {
    .hook = netfilter_monitor_packet,
    .pf = PF_INET,
    .hooknum = NF_INET_LOCAL_IN,	
	.priority = 1,
};

static struct nf_hook_ops netfilter_forward_ops = {
    .hook = netfilter_monitor_packet,
    .pf = PF_INET,
    .hooknum = NF_INET_FORWARD,	
	.priority = 1,
};

static struct nf_hook_ops netfilter_post_routing_ops = {
    .hook = netfilter_monitor_packet,
    .pf = PF_INET,
    .hooknum = NF_INET_POST_ROUTING,	
	.priority = 1,
};

static int rule_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Open netfilter rule\n");

    return 0;
}

static ssize_t rule_read(struct file *file, char __user *user_buffer, size_t size, loff_t *pos) {
    int len = 0;
    char buffer[512] = "";

    if (rule_head_for_proc_read == NULL)
        rule_head_for_proc_read = rule_head;

    else
        rule_head_for_proc_read = rule_head_for_proc_read->next;


    if (rule_head_for_proc_read != NULL) {
        len = sprintf(buffer, "src port: %d | change port: %d | type: %s\n",
            rule_head_for_proc_read->src_port,
            rule_head_for_proc_read->change_port,
            rule_head_for_proc_read->type == FORWARDING ? "Forwarding" : "Drop");

        copy_to_user(user_buffer, buffer, len);
    }

    return len;
}

static ssize_t rule_add(struct file *file, const char __user *user_buffer, size_t count, loff_t *pos) {
    char buffer[512] = "";
    char ops;
    Rule *new_rule = (Rule *)kmalloc(sizeof(Rule), GFP_KERNEL);

    if (copy_from_user(buffer, user_buffer, count)) {
        kfree(new_rule);
        printk(KERN_INFO "Rule add error\n");

        return count;
    };

    sscanf(buffer, "%d %d %c\n", &(new_rule->src_port), &(new_rule->change_port), &ops);
    new_rule->type = ops == 'F' ? FORWARDING : DROP;
    new_rule->next = NULL;

    if (rule_head == NULL)
        rule_head = new_rule;

    else {
        Rule *cur = rule_head;
        
        while (cur->next != NULL) {
            cur = cur->next;
        }

        cur->next = new_rule;
    }

    return count;
}

static const struct file_operations rule_fops = {
    .owner = THIS_MODULE,
    .open = rule_open,
    .read = rule_read,
    .write = rule_add,
};

static int __init my_netfilter_init(void) {
    printk(KERN_INFO "My netfilter init\n");
    proc_dir = proc_mkdir(PROC_DIRNAME, NULL);
    proc_file = proc_create(PROC_FILENAME, 0600, proc_dir, &rule_fops);

    nf_register_hook(&netfilter_pre_routing_ops);
    nf_register_hook(&netfilter_local_in_ops);
    nf_register_hook(&netfilter_forward_ops);
    nf_register_hook(&netfilter_post_routing_ops);

    return 0;
}

static void __exit my_netfilter_exit(void) {
    Rule *cur = rule_head;

    while (cur != NULL) {
        Rule *temp = cur;
        cur = cur->next;

        kfree(temp);
    }

    printk(KERN_INFO "My netfilter exit\n");
    remove_proc_entry(PROC_FILENAME, proc_dir);
    remove_proc_entry(PROC_DIRNAME, NULL);

    nf_unregister_hook(&netfilter_pre_routing_ops);
    nf_unregister_hook(&netfilter_local_in_ops);
    nf_unregister_hook(&netfilter_forward_ops);
    nf_unregister_hook(&netfilter_post_routing_ops);

    return;
}



module_init(my_netfilter_init);
module_exit(my_netfilter_exit);

MODULE_AUTHOR("Juhan Cha, Kyungha Kim");
MODULE_DESCRIPTION("Netfilter");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
