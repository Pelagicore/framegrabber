/******************************************************************************
 *
 *   Copyright (C) 2017 Pelagicore AB. All rights reserved.
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#define WIDTH 1280
#define HEIGHT 720

#define GET_FRAME 0

struct std_dma_desc {
        u32 rdaddr;
        u32 wraddr;
        u32 length;
        u32 ctrl;
};

#define DESC_RDADDR    0
#define DESC_WRADDR    1
#define DESC_LENGTH    2
#define DESC_CONTROL   3

#define DMA_STATUS     8
#define DMA_CONTROL    9
#define DMA_FILL_RW    10
#define DMA_FILL_RESP  11
#define DMA_SEQNO      12

#define STATUS_BUSY          (1 << 0)
#define STATUS_DESC_EMPTY    (1 << 1)
#define STATUS_DESC_FULL     (1 << 2)
#define STATUS_RESP_EMPTY    (1 << 3)
#define STATUS_RESP_FULL     (1 << 4)
#define STATUS_STOPPED       (1 << 5)
#define STATUS_RESETTING     (1 << 6)
#define STATUS_STOPPED_ERROR (1 << 7)
#define STATUS_STOPPED_EARLY (1 << 8)
#define STATUS_IRQ           (1 << 9)

#define CTRL_STOP_DISPATCHER  (1 << 0)
#define CTRL_RESET_DISPATCHER (1 << 1)
#define CTRL_STOP_ON_ERROR    (1 << 2)
#define CTRL_STOP_ON_EARLY    (1 << 3)
#define CTRL_IRQ_ENABLE       (1 << 4)
#define CTRL_STOP_DESCRIPTORS (1 << 5)

#define DESC_GO    (1 << 31)
#define DESC_EDE   (1 << 24)

struct arp_info {
	struct pci_dev *dev;
        dev_t cdev;
        unsigned char *user_fb;
        unsigned int bar0_base;
        unsigned int bar0_size;
        u8 *bar0_mem;
        unsigned int bar2_base;
        unsigned int bar2_size;
        u32 *bar2_mem;
        int chr_major;
};

struct arp_dma_buf {
        u8 *cpu_addr;
        dma_addr_t dma_addr;
        size_t size;
};

struct arp_info *ai = NULL;
static struct cdev arp_cdev;
static struct class *dev_class;

struct arp_dma_buf framebuffer;

const uint32_t offsets[] = {0x04000000,
                            0x10000000,
                            0x1c000000,
                            0x28000000};

static u32
dma_stop(void)
{
        int i;
        u32 val;

        ai->bar2_mem[DMA_CONTROL] = CTRL_STOP_DISPATCHER;
        for(i = 0;i < 1000;i++) {
                val = ai->bar2_mem[DMA_STATUS];
                if(val & STATUS_STOPPED) {
                        return val;
                }
                msleep(1);
        }
        return -1;
}

static u32
dma_reset(void)
{
        int i;
        u32 val;

        ai->bar2_mem[DMA_CONTROL] = CTRL_RESET_DISPATCHER;
        for(i = 0;i < 1000;i++) {
                val = ai->bar2_mem[DMA_STATUS];
                if((val & STATUS_RESETTING) == 0) {
                        return val;
                }
                msleep(1);
        }
        return -1;
}

static int
dma_transfer(u32 from, u32 to, u32 num_bytes)
{
        int i;
        u32 val;

        printk(KERN_INFO "from: %08x", from);
        printk(KERN_INFO "  to: %08x", to);
        printk(KERN_INFO " len: %08x", num_bytes);
        printk(KERN_INFO "ctrl: %08x", DESC_GO);

        ai->bar2_mem[DESC_RDADDR] = from;
        ai->bar2_mem[DESC_WRADDR] = to;
        ai->bar2_mem[DESC_LENGTH] = num_bytes;
        ai->bar2_mem[DESC_CONTROL] = DESC_GO;

        for(i = 0;i < 100;i++) {
                val = ai->bar2_mem[DMA_STATUS];
                printk(KERN_INFO "sts: %08x\n", val);
                if((val & STATUS_BUSY) == 0) {
                        return 0;
                }
                msleep(1);
        }
        return -1;
}

static int
arp_open (struct inode *inode, struct file *f)
{
        return (0);
}


static int
arp_close (struct inode *inode, struct file *f)
{
        return (0);
}

#define MOO

#define DMA_CHUNK_SIZE (64*1024)

static ssize_t
arp_read (struct file * f, char *buf, size_t count, loff_t * ppos)
{
        int chunks;
        int remain;
        dma_addr_t dma_addr;
        u8 *cpu_addr;
        int res;
        int c, i;

        chunks = count / DMA_CHUNK_SIZE;
        remain = count % DMA_CHUNK_SIZE;

        printk(KERN_INFO "dma_stop() - %08x\n", dma_stop());
        printk(KERN_INFO "dma_reset() - %08x\n", dma_reset());

        cpu_addr = dma_alloc_coherent(NULL, DMA_CHUNK_SIZE,
                                      &dma_addr, GFP_KERNEL);
        if(!cpu_addr) {
                printk(KERN_INFO "DMA alloc failed\n");
                return 0;
        }

        printk(KERN_INFO "dma_addr: - %llx\n", dma_addr);

#ifdef MOO
        for(i = 0;i < 32;i++) {
                ai->bar0_mem[0x10000000 + i] = i;
                cpu_addr[i] = i+32;
                printk(KERN_INFO "%02d: %02x ", i, ai->bar0_mem[0x10000000 + i]);
        }
        count = 32;
#endif

        for(c = 0;c < chunks;c++) {
                res = dma_transfer(c * DMA_CHUNK_SIZE,
                                   dma_addr, DMA_CHUNK_SIZE);
                dma_sync_single_for_cpu(NULL, dma_addr,
                                        DMA_CHUNK_SIZE, DMA_FROM_DEVICE);
                res = copy_to_user(buf + c * DMA_CHUNK_SIZE,
                                   cpu_addr, DMA_CHUNK_SIZE);
                if(res < DMA_CHUNK_SIZE) {
                        printk(KERN_INFO "Failed to copy to user space");
                        return 0;
                }
        }

        if(remain) {
                res = dma_transfer(c * DMA_CHUNK_SIZE, dma_addr, remain);
                dma_sync_single_for_cpu(NULL, dma_addr,
                                        DMA_CHUNK_SIZE, DMA_FROM_DEVICE);
                res = copy_to_user(buf + c * DMA_CHUNK_SIZE,
                                   cpu_addr, DMA_CHUNK_SIZE);
                if(res < DMA_CHUNK_SIZE) {
                        printk(KERN_INFO "Failed to copy to user space");
                        return 0;
                }
        }

#ifdef MOO
        for(i = 0;i < 32;i++) {
                printk(KERN_INFO "%02x ", cpu_addr[i]);
        }
#endif

        dma_free_coherent(NULL, DMA_CHUNK_SIZE, cpu_addr, dma_addr);
        return 0;
}


static ssize_t
arp_write (struct file * f, __user const char *buf, size_t count, loff_t * ppos)
{
        return 0;
}

static long arp_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
        u32 *s;
        u32 *t;
        int x, y;

        switch(cmd) {
                case GET_FRAME:
                        s = (u32 *)ai->bar0_mem;
                        t = (u32 *)arg;
                        for(y = 0;y < HEIGHT;y++) {
                                for(x = 0;x < WIDTH;x++) {
                                        *t++ = *s++;
                                }
                        }
                        break;
        }
        return 0;
}

static void arp_vma_close(struct vm_area_struct *vma)
{
    pci_free_consistent(ai->dev, framebuffer.size,
                        framebuffer.cpu_addr, framebuffer.dma_addr);
    framebuffer.cpu_addr = NULL; /* Mark as unused/free */
}

struct vm_operations_struct arp_vma_ops = {
    .close = arp_vma_close
};

static int arp_mmap(struct file *filp, struct vm_area_struct *vma)
{
        size_t size;

        size = vma->vm_end - vma->vm_start;

	if (pci_set_consistent_dma_mask(ai->dev, DMA_BIT_MASK(64))) {
		printk(KERN_WARNING
		       "mydev: No suitable DMA available.\n");
	}

        /* Allocate consistent memory that can be used for DMA transactions */
        if(!framebuffer.cpu_addr) {
                framebuffer.cpu_addr =
                        dma_alloc_coherent(NULL, size,
                                           &framebuffer.dma_addr, GFP_KERNEL);
                if (framebuffer.cpu_addr == NULL)
                        return -ENOMEM; /* Out of juice */
        }

        framebuffer.size = size;

        vma->vm_ops = &arp_vma_ops;
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
        vma->vm_private_data = NULL;

        /*
         * Map this DMA buffer into user space.
         */
        if (remap_pfn_range(vma, vma->vm_start,
                            framebuffer.dma_addr >> PAGE_SHIFT,
                            size, vma->vm_page_prot))
        {
                printk(KERN_CRIT "unable to remap memory\n");
                /* Out of luck, rollback... */
                pci_free_consistent(ai->dev, framebuffer.size,
                                    framebuffer.cpu_addr,
                                    framebuffer.dma_addr);
                framebuffer.cpu_addr = NULL;
                return -EAGAIN;
        }

        memcpy((char *)framebuffer.cpu_addr, "Linus", 6);
        return 0;
}

static struct file_operations arp_fops =
{
  .owner = THIS_MODULE,
  .read = arp_read,
  .write = arp_write,
  .open = arp_open,
  .release = arp_close,
  .unlocked_ioctl = arp_ioctl,
  .mmap = arp_mmap,
};

static int arp_pci_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	struct arp_info *info;

	info = kzalloc(sizeof(struct arp_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (pci_enable_device(dev))
		goto out_free;

	pci_set_master(dev);

	if (pci_request_regions(dev, "framegrabber"))
		goto out_disable;

	info->bar0_base = pci_resource_start(dev, 0);
	if (!info->bar0_base)
		goto out_release;

	info->bar0_size = pci_resource_len(dev, 0);
	info->bar0_mem = pci_ioremap_bar(dev, 0);
	if (!info->bar0_mem) {
		goto out_release;
	}

	info->bar2_base = pci_resource_start(dev, 2);
	if (!info->bar2_base)
		goto out_release;

	info->bar2_size = pci_resource_len(dev, 2);
	info->bar2_mem = pci_ioremap_bar(dev, 2);
	if (!info->bar2_mem) {
		goto out_release;
	}

        printk(KERN_INFO "BAR0: %x\n", info->bar0_base);
        printk(KERN_INFO "BAR2: %x\n", info->bar2_base);

	info->dev = dev;

        ai = info;

	pci_set_drvdata(dev, info);

        /* Allocating Major number */
        if((alloc_chrdev_region(&info->cdev, 0, 1, "framegrabber")) < 0){
                printk(KERN_INFO "Cannot allocate major number\n");
                goto out_unmap;
        }
        printk(KERN_INFO "Major = %d Minor = %d \n",
               MAJOR(info->cdev), MINOR(info->cdev));

        /* Creating cdev structure */
        cdev_init(&arp_cdev, &arp_fops);
        arp_cdev.owner = THIS_MODULE;
        arp_cdev.ops = &arp_fops; /* TODO: really necessary? */

        /* Adding character device to the system */
        if((cdev_add(&arp_cdev, info->cdev, 1)) < 0){
                printk(KERN_INFO "Cannot add the device to the system\n");
                goto out_region;
        }

        /* Creating struct class */
        if((dev_class = class_create(THIS_MODULE, "arp_class")) == NULL){
            printk(KERN_INFO "Cannot create the struct class\n");
            goto out_cdev;
        }

        /* Creating device */
        if((device_create(dev_class, NULL, info->cdev, NULL,"framegrabber")) == NULL) {
            printk(KERN_INFO "Cannot create the device\n");
            goto out_class;
        }

	return 0;
//out_device:
//        device_destroy(dev_class, info->cdev);
out_class:
        class_destroy(dev_class);
out_cdev:
        cdev_del(&arp_cdev);
out_region:
        unregister_chrdev_region(info->cdev, 1);
out_unmap:
	iounmap(info->bar0_mem);
out_release:
	pci_release_regions(dev);
out_disable:
	pci_disable_device(dev);
out_free:
	kfree (info);
	return -ENODEV;
}

static void arp_pci_remove(struct pci_dev *dev)
{
        struct arp_info *info = pci_get_drvdata(dev);

        device_destroy(dev_class, info->cdev);
        class_destroy(dev_class);
        cdev_del(&arp_cdev);
        unregister_chrdev_region(info->cdev, 1);
        pci_release_regions(dev);
        pci_disable_device(dev);
        iounmap(info->bar0_mem);

        kfree (info);
}

static struct pci_device_id arp_pci_ids[] = {
	{
		.vendor =	0x8086,
		.device =	0xfffd,
		.subvendor =	PCI_ANY_ID,
		.subdevice =	PCI_ANY_ID,
	},
	{ 0, }
};

static struct pci_driver arp_pci_driver = {
	.name = "framegrabber",
	.id_table = arp_pci_ids,
	.probe = arp_pci_probe,
	.remove = arp_pci_remove,
};

module_pci_driver(arp_pci_driver);

MODULE_DEVICE_TABLE(pci, arp_pci_ids);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Linus Nielsen");
