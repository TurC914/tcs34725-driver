#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/of.h> 
#include "TCS34725_ioctl.h"
#include <linux/mod_devicetable.h> 
#include <linux/delay.h>


#define DEVICE_NAME "TCS34725"                     // Ten cam bien
#define CLASS_NAME "TCS34725_class"                
#define DRIVER_NAME "TCS34725_driver"              
#define CMD_BIT            0x80                    // Command bit I2C
#define ENABLE_REGISTER    (CMD_BIT | 0x00)        
#define ATIME_REGISTER     (CMD_BIT | 0x01)        
#define CONTROL_REGISTER   (CMD_BIT | 0x0F)         
#define STATUS_REGISTER    (CMD_BIT | 0x13)
#define CLEAR_REGISTER     (CMD_BIT | 0x14)
#define RED_REGISTER       (CMD_BIT | 0x16)
#define GREEN_REGISTER     (CMD_BIT | 0x18)
#define BLUE_REGISTER      (CMD_BIT | 0x1A)
#define MAX_RETRY          10


static int major;
static struct class* tcs_class = NULL;
static struct device* tcs_device = NULL;
struct i2c_client* tcs_client = NULL; 
static int tcs_probe(struct i2c_client *client);
static void tcs_remove(struct i2c_client *client);

// Open
static int TCS34725_open(struct inode *device_file, struct file *instance)
{
    printk("TCS34725 device opened \n");
    return 0;
}

// Release
static int TCS34725_release(struct inode *device_file, struct file *instance)
{
    printk("TCS34725 device closed \n");
    return 0;
}
// Read
static ssize_t TCS34725_read(struct file *File, char *user_buffer, size_t count, loff_t *f_pos)
{
    if (!tcs_client)
        return -ENODEV;

    uint16_t c, r, g, b;
    uint8_t status;
    char buffer[128];
    int len, res;

  // check if the I2C client is available
    res = i2c_smbus_read_byte_data(tcs_client, STATUS_REGISTER);
    if (res < 0) return -EIO;

    status = res;
    if (!(status & 0x01)) {
        printk("Data not valid\n");
        return -EAGAIN;
    }


    int i;
    for (i = 0; i < MAX_RETRY; i++) {
    res = i2c_smbus_read_byte_data(tcs_client, STATUS_REGISTER);
    if (res < 0) return -EIO;

    status = res;

    if (status & 0x01)  // AVALID bit
        break;

    msleep(50); 
}

    if (!(status & 0x01)) {
        printk("TCS34725: Sensor data not valid yet (AVALID not set)\n");
        return -EAGAIN; 
    }

    // read color data
    res = i2c_smbus_read_word_data(tcs_client, CLEAR_REGISTER);
    if (res < 0) return -EIO;
    c = res;

    res = i2c_smbus_read_word_data(tcs_client, RED_REGISTER);
    if (res < 0) return -EIO;
    r = res;

    res = i2c_smbus_read_word_data(tcs_client, GREEN_REGISTER);
    if (res < 0) return -EIO;
    g = res;

    res = i2c_smbus_read_word_data(tcs_client, BLUE_REGISTER);
    if (res < 0) return -EIO;
    b = res;

    len = snprintf(buffer, sizeof(buffer), "Clear:%u Red:%u Green:%u Blue:%u\n", c, r, g, b);
    printk("Sensor read: %s", buffer);

    return copy_to_user(user_buffer, buffer, len) ? -EFAULT : len;
}


// Write
static ssize_t TCS34725_write(struct file *File, const char *user_buffer, size_t count, loff_t *offs)
{   
    if (!tcs_client)
        return -ENODEV;

    // Check if the I2C client is available
    char user_data[10]; // buffer to store data from user space
    memset(user_data, 0, 10); // clear buffer

    if (copy_from_user(user_data, user_buffer, count)) // copy data (buffer) from user space to kernel
    {
        printk("TCS34725 write failed \n");
        return -EFAULT;
    }
    else
        printk("TCS34725 write success \n User: %s\n", user_data);

    printk("TCS34725 write called \n");
    return count;
}

// file operations structure
static struct file_operations TCS34725_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = TCS34725_ioctl,
    .open = TCS34725_open,
    .read = TCS34725_read,
    .write = TCS34725_write,
    .release = TCS34725_release,
};

// I2C probe function
static int tcs_probe(struct i2c_client *client)
{   
    int ret;
    tcs_client = client;

    // POWER ON the sensor
    ret = i2c_smbus_write_byte_data(tcs_client, ENABLE_REGISTER, 0x03);
    if (ret < 0) return ret;
    msleep(800); // delay after enable
    // Set the integration time to 50ms
    ret = i2c_smbus_write_byte_data(tcs_client, ATIME_REGISTER, 0xEB);
    if (ret < 0) return ret;
    // Set the gain to 16x
    ret = i2c_smbus_write_byte_data(client, CONTROL_REGISTER, 0x02);
    if (ret < 0) return ret;
    printk("KERNEL: TCS34725 probe successful\n");
    return 0;
}

static void tcs_remove(struct i2c_client *client)
{
    printk("KERN_INFO : TCS34725 device removed\n");
    
}
// Device tree match table
static const struct of_device_id tcs34725_of_match[] = {
    { .compatible = "taos,tcs34725" },
    { }
};
MODULE_DEVICE_TABLE(of, tcs34725_of_match);

// I2C ID table
static const struct i2c_device_id tcs_id[] = {
    { "tcs34725", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, tcs_id);

// I2C driver structure
static struct i2c_driver tcs_driver = {
    .driver = {
        .name = "tcs34725",
        .of_match_table = of_match_ptr(tcs34725_of_match),
    },
    .probe = tcs_probe,
    .remove = tcs_remove,
    .id_table = tcs_id,
};


// Init
static int __init TCS34725_init(void)
{
    major = register_chrdev(0, DEVICE_NAME, &TCS34725_fops);
    if (major < 0) {
        printk("Failed to register a major number\n");
        return major;
    }

    tcs_class = class_create(CLASS_NAME);
    if (IS_ERR(tcs_class)) {
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(tcs_class);
    }

    tcs_device = device_create(tcs_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(tcs_device)) {
        class_destroy(tcs_class);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(tcs_device);
    }

    i2c_add_driver(&tcs_driver);
    printk(KERN_INFO "TCS34725: registered with major number %d\n", major);
    return 0;
}

//Exit 
static void __exit TCS34725_exit(void)
{
    class_destroy(tcs_class);
    unregister_chrdev(major, DEVICE_NAME);
    i2c_del_driver(&tcs_driver);
    device_destroy(tcs_class, MKDEV(major, 0));
    class_unregister(tcs_class);
    printk("TCS34725 driver unloaded\n");

}

module_init(TCS34725_init);
module_exit(TCS34725_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCS34725 Color Sensor Driver");
MODULE_AUTHOR("Minh To, Minh Diep, Thanh Nguyen");
MODULE_VERSION("V1");