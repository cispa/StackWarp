#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/sched/signal.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pid_namespace.h>
#include <linux/delay.h>
#include <asm/msr.h>
#include "msr_flipper.h"

#define countof(x) (sizeof(x)/sizeof(*(x)))

static void root_vma_close_callback(struct vm_area_struct *vma);

static struct mutex instance_lock;
static struct device* root_chardev = NULL;
static struct class* root_cls = NULL;
static struct cdev root_cdev;
static int root_major = -1;

struct page* comm_page_ref = NULL;
static volatile unsigned char* comm_page = NULL;
static const struct vm_operations_struct vm_ops = { .close = root_vma_close_callback, };

static volatile unsigned long jitter_msr = 0;
static volatile unsigned long jitter_mask = 0;
static volatile unsigned long jitter_reps = 0;

static struct mapped_process {
    pid_t pid; 
    unsigned long uaddr;
} mapped_processes[16] = {0, };

static inline struct task_struct *find_task_by_pid_ns_(pid_t nr, struct pid_namespace *ns) {
	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "find_task_by_pid_ns() needs rcu_read_lock() protection");
	return pid_task(find_pid_ns(nr, ns), PIDTYPE_PID);
}

static inline struct task_struct *find_task_by_vpid_(pid_t vnr) {
	return find_task_by_pid_ns_(vnr, task_active_pid_ns(current));
}

static struct task_struct *find_get_task_by_vpid_(pid_t nr) {
	struct task_struct *task;

	rcu_read_lock();
	task = find_task_by_vpid_(nr);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();

	return task;
}

static int unmap_user_page(pid_t pid, unsigned long user_addr) {
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    int ret = 0;

    // Find the task struct by PID
    task = find_get_task_by_vpid_(pid);
    if (!task) {
        pr_err("Task with PID %d not found\n", pid);
        return -ESRCH;
    }

    mm = get_task_mm(task);
    if (!mm) {
        pr_err("Failed to get mm_struct for PID %d\n", pid);
        put_task_struct(task);
        return -EINVAL;
    }

    // Lock the address space
    down_write(&mm->mmap_lock);

    vma = find_vma(mm, user_addr);
    if (!vma || user_addr < vma->vm_start) {
        pr_err("Address 0x%lx not valid in process PID %d\n", user_addr, pid);
        ret = -EFAULT;
        goto out_unlock;
    }

    // zap_page_range is aligned to PAGE_SIZE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    zap_page_range_single(vma, user_addr & PAGE_MASK, PAGE_SIZE, NULL);
#else
    zap_page_range(vma, user_addr & PAGE_MASK, PAGE_SIZE);
#endif

out_unlock:
    up_write(&mm->mmap_lock);
    mmput(mm);
    put_task_struct(task);
    return ret;
}

static void root_vma_close_callback(struct vm_area_struct *vma) {
    unsigned int i;

    if (!comm_page_ref)
        return;
    
    mutex_lock(&instance_lock);

    for (i = 0; i < countof(mapped_processes); i++) {
        if (mapped_processes[i].pid == current->pid) {
            mapped_processes[i] = (struct mapped_process) {0, 0};
        }
    }

    mutex_unlock(&instance_lock);
}

static int root_mmap(struct file *file, struct vm_area_struct *vma) {
    unsigned int i, check;
    int rc = -EINVAL;

    // There is only a single page
    if (vma->vm_end - vma->vm_start != PAGE_SIZE)
        return -EINVAL;    

    mutex_lock(&instance_lock);
    
    for (i = 0, check = 0; i < countof(mapped_processes); i++) {
        // A process can only map us once
        if (mapped_processes[i].pid == current->pid)
            goto exit;
        // We still need room to register the process
        if (!mapped_processes[i].pid)
            check = 1;
    }
    if (!check)
        goto exit;

    // Map page, do registration
    rc = vm_insert_page(vma, vma->vm_start, comm_page_ref);
    if (rc < 0)
        goto exit;

    vma->vm_ops = &vm_ops;
    for (i = 0, check = 0; i < countof(mapped_processes); i++) {
        if (!mapped_processes[i].pid) {
            mapped_processes[i] = (struct mapped_process) {current->pid, vma->vm_start};
            break;
        }
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    vm_flags_set(vma, VM_SHARED | VM_MAYWRITE);
#else
    vma->vm_flags |= VM_SHARED | VM_MAYWRITE;
#endif

exit:
    mutex_unlock(&instance_lock);
    return rc;
}

// #define RDMSR0 1
static int read_msr_on_cpu(unsigned int cpu, unsigned long msr, unsigned long *value) {
#ifndef RDMSR0
    unsigned int hi, lo;
    int ret = rdmsr_on_cpu(cpu, (unsigned int) msr, &lo, &hi);

    *value = (unsigned long) lo | ((unsigned long)hi << 32);

    return ret;
#else
    (void) cpu;
    (void) msr;
    (void) value;

    return 0;
#endif
}

static void msr_write_ipi(void *_arg) {
    unsigned long msr = ((unsigned long*) _arg)[0];
    unsigned long val = ((unsigned long*) _arg)[1];

    wrmsrl(msr, val);
}

static int write_msr_on_cpu(unsigned int cpu, unsigned long msr, unsigned long value) {
    unsigned long arg[] = {msr, value};

    if (!cpu_online(cpu))
        return -ENODEV;

    return smp_call_function_single(cpu, msr_write_ipi, &arg, /* wait */ true);
}

// Flip MSR bits back and forth quickly
// Optionally sync with user mode through shared memory
static int msr_jitter(void* _arg) {
    unsigned int i;
    unsigned long msr = jitter_msr;
    unsigned long val = 0;
    unsigned long mask = jitter_mask;

    usleep_range(1, 10);
    cond_resched();
    rdmsrl(msr, val);

    *comm_page = 1;
    for (i = 0; i < (jitter_reps ? jitter_reps : 10000); i++) {
        val ^= mask;
        wrmsrl(msr, val);
        // TODO maybe add a small delay
    }
    *comm_page = 0;


    return 0;
}

static inline unsigned long __attribute__((always_inline)) local_rdtsc(void) {
    unsigned long hi, lo;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return (hi << 32) | lo;
}

static inline void __attribute__((always_inline)) local_wrmsr(unsigned int msr, unsigned long val) {
    asm volatile ("wrmsr" :: "a" (val & ((1ul << 32) - 1)), "d" (val >> 32), "c"(msr));
}

static int msr_jitter_await(void* _arg) {
    // unsigned int i;
    unsigned long msr = jitter_msr;
    unsigned long val = 0;
    unsigned long mask = jitter_mask;
    unsigned long wait_tsc;
    unsigned long orig_val;
    unsigned long sp_dest[2];

    cond_resched();

    rdmsrl(msr, orig_val);
    wrmsrl(msr, orig_val ^ mask);
    rdmsrl(msr, val);
    wrmsrl(msr, orig_val);
    if (val != (orig_val ^ mask)) {
        comm_page[0] = 1;
        comm_page[3] = 1;
        return 1;
    }

    val = orig_val;

    while (!comm_page[2])
        cond_resched();
    comm_page[0] = 1;
    wait_tsc = local_rdtsc();
    while (!comm_page[1] && (local_rdtsc() - wait_tsc) < 0x40000000) {}
    /*for (i = 0; i < 10000 && comm_page[1]; i++) {
        val ^= mask;
        wrmsrl(msr, val);
        // TODO maybe add a small delay
    }*/

    asm volatile (
        "mov %%rsp, (%0)\n"
        "mov %%rbp, 8(%0)\n"
        "mfence\n"
        ".L1:\n"
        "xor %1, %2\n"
        "mov %2, %%rax\n"
        "mov %2, %%rdx\n"
        "shr $32, %%rdx\n"
        "wrmsr\n"
        // ".fill 1000, 1, 0x90\n"
        "dec %%rdi\n"
        "jnz .L1\n"
        "mov 8(%0), %%rbp\n"
        "mov (%0), %%rsp\n"
        "mfence\n"
        :
        : "r"(sp_dest), "r"(mask), "r"(val), "c"(msr), "D"(jitter_reps ? jitter_reps : 10000)
        : "rax", "rdx", "memory"
    );

    wrmsrl(msr, orig_val);
    comm_page[0] = 0;

    return 0;
}

static int msr_jitter_await_slow(void* _arg) {
    unsigned int i;
    unsigned long msr = jitter_msr;
    unsigned long val = 0;
    unsigned long mask = jitter_mask;
    unsigned long wait_tsc;
    unsigned long orig_val;

    cond_resched();

    rdmsrl(msr, orig_val);
    wrmsrl(msr, orig_val ^ mask);
    rdmsrl(msr, val);
    wrmsrl(msr, orig_val);
    if (val != (orig_val ^ mask)) {
        comm_page[0] = 1;
        comm_page[3] = 1;
        return 1;
    }

    val = orig_val;

    while (!comm_page[2])
        cond_resched();
    comm_page[0] = 1;
    wait_tsc = local_rdtsc();
    while (!comm_page[1] && (local_rdtsc() - wait_tsc) < 0x40000000) {}
    for (i = 0; i < 10000 && comm_page[1]; i++) {
        val ^= mask;
        wrmsrl(msr, val);
		usleep_range(1, 10);
    }

    wrmsrl(msr, orig_val);
    comm_page[0] = 0;

    return 0;
}

static int msr_flip_single(void* _arg) {
    unsigned long msr = jitter_msr;
    unsigned long val = 0;
    unsigned long mask = jitter_mask;
    unsigned long wait_tsc;
    unsigned long orig_val;
    unsigned long sp_dest[2];

    cond_resched();
    rdmsrl(msr, orig_val);
    val = orig_val;

    while (!comm_page[2])
        cond_resched();
    comm_page[0] = 1;
    wait_tsc = local_rdtsc();
    while (!comm_page[1] && (local_rdtsc() - wait_tsc) < 0x40000000) {}

    asm volatile (
        "mov %%rsp, (%0)\n"
        "mov %%rbp, 8(%0)\n"
        "mfence\n"
        ".rept 4\n"
        "xor %1, %2\n"
        "mov %2, %%rax\n"
        "mov %2, %%rdx\n"
        "shr $32, %%rdx\n"
        "wrmsr\n"
        // ".fill 1000, 1, 0x90\n"
        ".endr\n"
        "mov 8(%0), %%rbp\n"
        "mov (%0), %%rsp\n"
        "mfence\n"
        :
        : "r"(sp_dest), "r"(mask), "r"(val), "c"(msr)
        : "rax", "rdx", "memory"
    );

    local_wrmsr(msr, orig_val);
    comm_page[0] = 0;

    return 0;
}

static long root_ioctl(struct file *, unsigned int cmd, unsigned long arg_addr) {
    long rc = 0;
    struct msr_flipper_arg arg;
    struct task_struct *task;

    mutex_lock(&instance_lock);
    
    if (copy_from_user(&arg, (void* __user) arg_addr, sizeof(arg)) != 0) {
        rc = -EFAULT;
        goto exit;
    }

    *comm_page = 0;
    jitter_msr = arg.id;
    jitter_mask = arg.mask;
    jitter_reps = arg.reps;
    comm_page[3] = 0;
    
    switch (cmd) {
        case IOCTL_MSR_FLIPPER_RD:
            rc = read_msr_on_cpu(arg.core, arg.id, &arg.value);
            break;
        case IOCTL_MSR_FLIPPER_WR:
            rc = write_msr_on_cpu(arg.core, arg.id, arg.value);
            break;
        case IOCTL_MSR_FLIPPER_FLIP:
            rc = read_msr_on_cpu(arg.core, arg.id, &arg.value);
            if (rc)
                break;
            // printk(KERN_INFO "Flip MSR %lx from 0x%016lx to 0x%016lx\n", arg.id, arg.value, arg.value ^ arg.mask);
            arg.value ^= arg.mask;
            rc = write_msr_on_cpu(arg.core, arg.id, arg.value);
            break;
        case IOCTL_MSR_FLIPPER_JITTER:
            task = kthread_create(msr_jitter, NULL, "msr_jitter");
            if (IS_ERR(task)) {
                pr_err("Failed to create kthread\n");
                rc = -EFAULT;
                break;
            }
            kthread_bind(task, arg.core);
            wake_up_process(task);
            break;
        case IOCTL_MSR_FLIPPER_JITTER_AWAIT:
            task = kthread_create(msr_jitter_await, NULL, "msr_jitter_await");
            if (IS_ERR(task)) {
                pr_err("Failed to create kthread\n");
                rc = -EFAULT;
                break;
            }
            kthread_bind(task, arg.core);
            wake_up_process(task);
            break;
        case IOCTL_MSR_FLIPPER_FLIP_SINGLE:
            task = kthread_create(msr_flip_single, NULL, "msr_flip_single");
            if (IS_ERR(task)) {
                pr_err("Failed to create kthread\n");
                rc = -EFAULT;
                break;
            }
            kthread_bind(task, arg.core);
            wake_up_process(task);
            break;
		case IOCTL_MSR_FLIPPER_FLIP_BUGGY:
			asm volatile (
				"wrmsr\n"
				"mov $0x1890, %%rcx\n"
				"mov $0x2000000, %%rax\n"
				"wrmsr\n"
				"dec %%rax\n"
				"wrmsr\n"
				"mov $0x2ff, %%rcx\n"
				"mov $0xc00, %%rax\n"
				"wrmsr\n"
				:: "a"(0), "c"(0x2ff), "d"(0));
			printk(KERN_INFO "I am alive\n");
			break;
			read_msr_on_cpu(arg.core, arg.id, &arg.value);
            printk(KERN_INFO "Flip MSR %lx from 0x%016lx to 0x%016lx\n", arg.id, arg.value, 0 ^ arg.mask);
			cond_resched();
			arg.value = 0;
            arg.value ^= arg.mask;
            rc = write_msr_on_cpu(arg.core, arg.id, arg.value);
            break;
		case IOCTL_MSR_FLIPPER_JITTER_AWAIT_SLOW:
            task = kthread_create(msr_jitter_await_slow, NULL, "msr_jitter_await_slow");
            if (IS_ERR(task)) {
                pr_err("Failed to create kthread\n");
                rc = -EFAULT;
                break;
            }
            kthread_bind(task, arg.core);
            wake_up_process(task);
            break;
        default:;
            rc = -ENOTTY;
    }

    if (copy_to_user((void* __user) arg_addr, &arg, sizeof(arg)) != 0)
        rc = -EFAULT;
    
exit:
    mutex_unlock(&instance_lock);
    return rc;
}

static int create_chardev(int* major, struct class ** cls, struct device ** chardev, struct cdev* cdev, const struct file_operations* fops, const char* name) {
    if (alloc_chrdev_region(major, 0, 1, name) < 0) {
        printk(KERN_ALERT "Example failed to register a major number\n");
        return *major;
    }

    // Register the device class
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0) 
    *cls = class_create(name);
    #else 
    *cls = class_create(THIS_MODULE, name); 
    #endif 

    if (IS_ERR(*cls)) {
        unregister_chrdev_region(*major, 1);
        printk(KERN_ALERT "Failed to register device class\n");
        return -1;
    }

    *chardev = device_create(*cls, NULL, *major, NULL, name);
    if (IS_ERR(*chardev )) {
        class_destroy(*cls);
        unregister_chrdev_region(*major, 1);
        return -1;
    }

    cdev_init(cdev, fops);
    if (cdev_add(cdev, *major, 1) < 0) {
        device_destroy(*cls, *major);
        class_destroy(*cls);
        unregister_chrdev_region(*major, 1);
        printk(KERN_ALERT "Failed to add cdev\n");
        return -1;
    }
    
    return 0;
}

static void remove_chardev(int major, struct class* cls, struct cdev* cdev, const char* name) {
    cdev_del(cdev);
    device_destroy(cls, major);
    class_destroy(cls);
    unregister_chrdev_region(major, 1);
}

static int root_open(struct inode *inode, struct file *file) {
    return 0;
}
static int root_close(struct inode *inode, struct file *file) {
    return 0;
}
static ssize_t root_read(struct file*, char*, size_t, loff_t*) {
    return -ENODEV;
}
static ssize_t root_write(struct file*, const char*, size_t, loff_t*) {
    return -ENODEV;
}

static struct file_operations fops_root = {
    .open = root_open,
    .release = root_close,
    .read = root_read,
    .write = root_write,
    .mmap = root_mmap,
    .unlocked_ioctl = root_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
};

static int __init msr_flipper_init(void) {
    comm_page_ref = alloc_pages(GFP_KERNEL, 0);
    if (!comm_page_ref) {
        pr_err("Failed to allocate comm page\n");
        return -ENOMEM;
    }
    SetPageReserved(comm_page_ref);
    comm_page = page_address(comm_page_ref);
    if (!comm_page) {
        __free_pages(comm_page_ref, 0);
        pr_err("This should never happen\n");
        return -ENOMEM;
    }
    memset((void*) comm_page, 0, PAGE_SIZE);
    mutex_init(&instance_lock);

    printk(KERN_INFO "Greetings\n");
    return create_chardev(&root_major, &root_cls, &root_chardev, &root_cdev, &fops_root, MSR_FLIPPER_FILE_NAME);
}

static void __exit msr_flipper_exit(void) {
    unsigned int i;

    remove_chardev(root_major, root_cls, &root_cdev, MSR_FLIPPER_FILE_NAME);

    for (i = 0; i < countof(mapped_processes); i++) {
        if (mapped_processes[i].pid)
            unmap_user_page(mapped_processes[i].pid, mapped_processes[i].uaddr);
    }

    mutex_destroy(&instance_lock);
    ClearPageReserved(comm_page_ref);
    __free_pages(comm_page_ref, 0);
    comm_page = NULL;
    comm_page_ref = NULL;
    printk(KERN_INFO "Interface removed, all is good\n");
    return;
}

module_init(msr_flipper_init);
module_exit(msr_flipper_exit);

MODULE_LICENSE("GPL v2");
