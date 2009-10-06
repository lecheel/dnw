/*
 * dnwOTG USB driver - 1.0
 *
 *     This file is licensed under the GPL. See COPYING in the package.
 * Based on usb-skeleton.c 2.0 by Greg Kroah-Hartman (greg@kroah.com)
 *
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>


#define DRIVER_AUTHOR "ryan.chang@quantatw.com"
#define DRIVER_DESC "Quanta DNW OTG USB Driver"

/* Define these values to match your devices */
//Bus 005 Device 010: ID 04e8:1234 Samsung Electronics Co., Ltd
#define USB_DNW_VENDOR_ID	0x04E8
#define USB_DNW_PRODUCT_ID	0x1234

/* table of devices that work with this driver */
static struct usb_device_id dnwOTG_table [] = {
    { USB_DEVICE(USB_DNW_VENDOR_ID, USB_DNW_PRODUCT_ID) },
    { }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, dnwOTG_table);

/* to prevent a race between open and disconnect */
static DEFINE_MUTEX(dnwOTG_open_lock);


/* Get a minor range for your devices from the usb maintainer */
#define USB_DNW_MINOR_BASE	192

/* our private defines. if this grows any larger, use your own .h file */
#define MAX_TRANSFER		(PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */
#define WRITES_IN_FLIGHT	8
/* arbitrarily chosen */

/* Structure to hold all of our device specific stuff */
struct usb_dnwOTG {
    struct usb_device		*udev;			/* the usb device for this device */
    struct usb_interface	*interface;		/* the interface for this device */
    struct semaphore		limit_sem;		/* limiting the number of writes in progress */
    unsigned char           	*bulk_in_buffer;	/* the buffer to receive data */
    size_t			bulk_in_size;		/* the size of the receive buffer */
    __u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
    __u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
    struct kref			kref;
    struct mutex		io_mutex;		/* synchronize I/O with disconnect */
};

#define to_dnwOTG_dev(d) container_of(d, struct usb_dnwOTG, kref)

static struct usb_driver dnwOTG_driver;

int dnwOTG_SND(struct usb_dnwOTG *dnwOTGA2,
        __u8 request, __u8 requesttype,
        __u16 value, __u16 index, __u8 len)
{
    char *dummy_buffer = kzalloc(4, GFP_KERNEL);
    int result;
    if (!dummy_buffer)
        return -ENOMEM;
    result = usb_control_msg(dnwOTGA2->udev,
            usb_sndctrlpipe(dnwOTGA2->udev, 0),
            request, requesttype, value, index,
            dummy_buffer, len, 1000);
    kfree(dummy_buffer);
    return result;
}

static inline void dnwOTG_RCV(struct usb_dnwOTG *dnwOTGA2,
        __u8 request, __u8 requesttype,
        __u16 value, __u16 index,
        char *buf, __u8 len)
{
    int result;
    result = usb_control_msg(dnwOTGA2->udev,
            usb_rcvctrlpipe(dnwOTGA2->udev, 0),
            request, requesttype, value, index,
            buf, len, 1000);
}

static void dnwOTG_delete(struct kref *kref)
{
    struct usb_dnwOTG *dev = to_dnwOTG_dev(kref);

    usb_put_dev(dev->udev);
    kfree(dev->bulk_in_buffer);
    kfree(dev);
}

static int dnwOTG_open(struct inode *inode, struct file *file)
{
    struct usb_dnwOTG *dev;
    struct usb_interface *interface;
    int subminor;
    int retval = 0;

    subminor = iminor(inode);
//    info("DNW(i):: Amba open");
    mutex_lock(&dnwOTG_open_lock);
    interface = usb_find_interface(&dnwOTG_driver, subminor);
    if (!interface) {
        mutex_unlock(&dnwOTG_open_lock);
        err ("%s - error, can't find device for minor %d",
                __FUNCTION__, subminor);
        retval = -ENODEV;
        goto exit;
    }

    dev = usb_get_intfdata(interface);
    if (!dev) {
        mutex_unlock(&dnwOTG_open_lock);
        retval = -ENODEV;
        goto exit;
    }

    /* increment our usage count for the device */
    kref_get(&dev->kref);
    /* now we can drop the lock */
    mutex_unlock(&dnwOTG_open_lock);

    if (retval) {
        kref_put(&dev->kref, dnwOTG_delete);
        goto exit;
    }

    /* save our object in the file's private structure */
    file->private_data = dev;

exit:
    return retval;
}

static int dnwOTG_release(struct inode *inode, struct file *file)
{
    struct usb_dnwOTG *dev;

    dev = (struct usb_dnwOTG *)file->private_data;
    if (dev == NULL)
        return -ENODEV;

    /* allow the device to be autosuspended */
    mutex_lock(&dev->io_mutex);
    mutex_unlock(&dev->io_mutex);

    /* decrement the count on our device */
    kref_put(&dev->kref, dnwOTG_delete);
    return 0;
}

static ssize_t dnwOTG_write(struct file *file, const char *user_buffer, size_t count, loff_t *ppos)
{
	struct usb_dnwOTG *dev;
        char *DNW_buf = NULL;
        int retval = 0, r;
	struct urb *urb = NULL;
	char *buf = NULL;
        int bytes_write=0;
	//int write_size=min(count, (size_t)MAX_TRANSFER);
        int write_size=count;
	
	dev = (struct usb_dnwOTG *)file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;

	DNW_buf = kzalloc(write_size, GFP_KERNEL);
   	if (!DNW_buf) {
        	err("Out of memory 128K");
        	return -ENOMEM;
    	}


	r = down_interruptible(&dev->limit_sem);
	if (r < 0)
		return -EINTR;

	
	if (copy_from_user(DNW_buf, user_buffer, write_size)) {
		retval = -EFAULT;
		goto exit;
	}

	dev->bulk_out_endpointAddr = 0x02;
    	retval = usb_bulk_msg(dev->udev,
            usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
            DNW_buf,
            write_size,
            &bytes_write, 5000);
	printk("Ryinfo:bytes_writed: %x(write_size=%x)\n",bytes_write,write_size);
	/* if the write was successful, return writen size */
        if (write_size == bytes_write) {
            retval = bytes_write;
	}else{
            retval = -EFAULT;
    	}

exit:
        kfree(DNW_buf);
	up(&dev->limit_sem);
	return retval;

}

static ssize_t dnwOTG_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
    struct usb_dnwOTG *dev;
    int retval;
    int bytes_read;
    char *DNW_buf;

    DNW_buf = kzalloc(128*1024, GFP_KERNEL);
    if (!DNW_buf) {
        err("Out of memory 128K");
        return -ENOMEM;
    }

    dev = (struct usb_dnwOTG *)file->private_data;

    mutex_lock(&dev->io_mutex);
    if (!dev->interface) {		/* disconnect() was called */
        retval = -ENODEV;
        goto exit;
    }

    /* do a blocking bulk read to get data from the device */
    dev->bulk_in_endpointAddr = 0x81;
    dev->bulk_in_size = 128*1024;
    retval = usb_bulk_msg(dev->udev,
            usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
            DNW_buf,
            min(dev->bulk_in_size, count),
            &bytes_read, 5000);

    /* if the read was successful, copy the data to userspace */
    if (!retval) {
        if (copy_to_user(buffer, DNW_buf, bytes_read))
            retval = -EFAULT;
        else
            retval = bytes_read;
    }

exit:
    kfree(DNW_buf);
    mutex_unlock(&dev->io_mutex);
    return retval;

}

/**
 * dnwOTG_ioctl
 */
static int dnwOTG_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
    struct usb_dnwOTG *dev;
    dev = (struct usb_dnwOTG *)file->private_data;

    return 0;
}

static const struct file_operations dnwOTG_fops = {
    .owner =		THIS_MODULE,
    //.read =		dnwOTG_read,
    .write = 		dnwOTG_write,
    .open =		dnwOTG_open,
    //.ioctl =        	dnwOTG_ioctl,
    .release =		dnwOTG_release,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver dnwOTG_class = {
    .name =		"dnwOTG",
    .fops =		&dnwOTG_fops,
    .minor_base =	USB_DNW_MINOR_BASE,
};


/*
 * Initialize device controls.
 */
int dnwOTG_init_device(struct usb_dnwOTG *dev)
{
    char *buf = kzalloc(4, GFP_KERNEL);
    int retval;
    if (!buf)
        return -ENOMEM;
    info("DNW(i):: DNW INIT USB device");
//    retval=dnwOTG_SND(dev,0x01,0x21,0x3100,0xf000,20);
    kfree(buf);
    return 0;
}

static int dnwOTG_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    struct usb_dnwOTG *dev;
    int retval = -ENOMEM;

    /* allocate memory for our device state and initialize it */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        err("Out of memory");
        goto error;
    }
    kref_init(&dev->kref);
    sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
    mutex_init(&dev->io_mutex);

    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;
//    usb_set_interface(dev->udev,interface->altsetting->desc.bInterfaceNumber,1);

    /* save our data pointer in this interface device */
    usb_set_intfdata(interface, dev);

    /* Initialize controls */
    if (dnwOTG_init_device(dev) < 0) {
        err("Cound not init dnwOTG device");
        goto error;
    }

    /* we can register the device now, as it is ready */
    retval = usb_register_dev(interface, &dnwOTG_class);
    if (retval) {
        /* something prevented us from registering this driver */
        err("Not able to get a minor for this device.");
        usb_set_intfdata(interface, NULL);
        goto error;
    }

    /* let the user know what node this device is now attached to */
    info("dnwOTGUSB device now attached to dnwOTGUSB-%d", interface->minor);
    return 0;

error:
    if (dev)
        /* this frees allocated memory */
        kref_put(&dev->kref, dnwOTG_delete);
    return retval;
}

static void dnwOTG_disconnect(struct usb_interface *interface)
{
    struct usb_dnwOTG *dev;
    int minor = interface->minor;

    /* prevent dnwOTG_open() from racing dnwOTG_disconnect() */
    mutex_lock(&dnwOTG_open_lock);

    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);


    /* give back our minor */
    usb_deregister_dev(interface, &dnwOTG_class);
    mutex_unlock(&dnwOTG_open_lock);

    /* prevent more I/O from starting */
    mutex_lock(&dev->io_mutex);
    dev->interface = NULL;
    mutex_unlock(&dev->io_mutex);



    /* decrement our usage count */
    kref_put(&dev->kref, dnwOTG_delete);

    info("dnwOTGUSB #%d now disconnected", minor);
}

static struct usb_driver dnwOTG_driver = {
    .name =	"DNW",
    .probe =	dnwOTG_probe,
    .disconnect =	dnwOTG_disconnect,
    .id_table =	dnwOTG_table,
};

static int __init usb_dnwOTG_init(void)
{
    int result;

    /* register this driver with the USB subsystem */
    result = usb_register(&dnwOTG_driver);
    if (result)
        err("usb_register failed. Error number %d", result);

    return result;
}

static void __exit usb_dnwOTG_exit(void)
{
    /* deregister this driver with the USB subsystem */
    usb_deregister(&dnwOTG_driver);
}

module_init(usb_dnwOTG_init);
module_exit(usb_dnwOTG_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
