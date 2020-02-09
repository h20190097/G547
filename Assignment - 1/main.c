#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>                 //kmalloc()
#include <linux/uaccess.h>              //copy_to/from_user()
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/random.h>
#include <linux/ioctl.h>

#define MAGIC_NUM 'A'
#define CHANNEL 'a'
#define ALLIGNMENT 'b'
#define SET_CHANNEL _IOW(MAGIC_NUM, CHANNEL, int32_t*)
#define SET_ALLIGNMENT _IOW(MAGIC_NUM, ALLIGNMENT, int32_t*)

static dev_t adc;              //Global variable for the first device number

static struct cdev c_dev;
static struct class *cls;
uint16_t adc_val;
uint16_t channel;
uint16_t allignment;

uint16_t getrand(void)
{
	uint16_t rnd_val;
	get_random_bytes(&rnd_val, sizeof(rnd_val));
	return rnd_val;
}

//Step 4 : Driver call back functions

static int adc_open(struct inode *i, struct file *f)
{
	printk(KERN_INFO "adc: open()\n");
	return 0;
}

static int adc_release(struct inode *i, struct file *f)
{
	printk(KERN_INFO "adc: close()\n");
	return 0;
}

static ssize_t adc_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	printk(KERN_INFO "adc: read()\n");
	adc_val = getrand() & 0x3ff;
	printk(KERN_INFO "ADC value : %d \n",adc_val);
	//adc_val = adc_val << 6;
	printk(KERN_INFO "Channel : %d \n",channel);
	printk(KERN_INFO "Allignment : %d \n",allignment);
	if(allignment == 1)
	{
		adc_val = adc_val << 6;
		printk(KERN_INFO "ADC value at higher bits: %d \n",adc_val);
	}
	copy_to_user(buf, &adc_val, sizeof(adc_val));
	return 0;
}

static long adc_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		case SET_CHANNEL:
			copy_from_user(&channel ,(int32_t*) arg, sizeof(channel));
			break;
		case SET_ALLIGNMENT:
			copy_from_user(&allignment ,(int32_t*) arg, sizeof(allignment));
			break;
	}
	return 0;
}

static struct file_operations fops = 
				{
					.owner  = THIS_MODULE,
					.open   = adc_open,
					.release= adc_release,
					.read   = adc_read,
					.unlocked_ioctl = adc_ioctl,
				};

static int __init adcdrv_init(void)    //constructor
{
	printk(KERN_INFO "myadc registered\n\n");
	//Step 1: rederve <major, minor>
	if (alloc_chrdev_region(&adc, 0, 1, "ADCDD") < 0)
	{
		return -1;
	}
	
	//Step 2 : creation of device file
	if ((cls=class_create(THIS_MODULE, "chardev"))==NULL)
	{
		unregister_chrdev_region(adc,1);
		return -1;
	}
	if(device_create(cls, NULL, adc, NULL, "adc8")==NULL)
	{
		class_destroy(cls);
		unregister_chrdev_region(adc,1);
		return -1;
	}

	// Step 3: Link fops and cdev to the device node
	cdev_init(&c_dev, &fops);
	if(cdev_add(&c_dev, adc, 1)==-1)
	{
		device_destroy(cls, adc);
		class_destroy(cls);
		unregister_chrdev_region(adc,1);
		return -1;
	}
	return 0;
}

static void __exit adcdrv_exit(void)      //Destructor
{
	cdev_del(&c_dev);
	device_destroy(cls, adc);
	class_destroy(cls);
	unregister_chrdev_region(adc, 1);
	printk(KERN_INFO "myadc unregistered");
}

module_init(adcdrv_init);        
module_exit(adcdrv_exit);

MODULE_DESCRIPTION("ADC module");
MODULE_AUTHOR("Tushar patil <tusharpatil10397@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_INFO(SupportedChips, "PCA9685, FT232RL");

