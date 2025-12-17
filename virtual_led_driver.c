#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/version.h>

#define DRIVER_NAME "virtual_led"
#define DEVICE_NAME "vled"
#define CLASS_NAME "vled"
#define MAX_DEVICES 1

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shelestov");
MODULE_DESCRIPTION("Virtual USB LED Driver with GUI control");
MODULE_VERSION("2.2");

static int major_number;
static struct class *vled_class = NULL;
static struct device *vled_device = NULL;
static struct cdev vled_cdev;

// Структура состояния устройства
struct vled_device_data {
    int led_state;          // 0 - выключен, 1 - включен
    int brightness;         // Яркость 0-255
    char color[16];         // Цвет светодиода
    struct mutex lock;      // Мьютекс для синхронизации
};

static struct vled_device_data device_data;

// Функции для работы с файловой системой
static int vled_open(struct inode *inodep, struct file *filep)
{
    struct vled_device_data *dev_data;
    dev_data = kmalloc(sizeof(struct vled_device_data), GFP_KERNEL);
    if (!dev_data)
        return -ENOMEM;
    
    mutex_init(&dev_data->lock);
    dev_data->led_state = 0;
    dev_data->brightness = 128;
    strcpy(dev_data->color, "green");
    
    filep->private_data = dev_data;
    return 0;
}

static int vled_release(struct inode *inodep, struct file *filep)
{
    struct vled_device_data *dev_data = filep->private_data;
    mutex_destroy(&dev_data->lock);
    kfree(dev_data);
    return 0;
}

static ssize_t vled_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    struct vled_device_data *dev_data = filep->private_data;
    char state_info[256];
    int bytes_to_copy;
    
    if (*offset > 0)
        return 0;
    
    mutex_lock(&dev_data->lock);
    snprintf(state_info, sizeof(state_info), 
             "LED State: %s\nBrightness: %d\nColor: %s\n",
             dev_data->led_state ? "ON" : "OFF",
             dev_data->brightness,
             dev_data->color);
    mutex_unlock(&dev_data->lock);
    
    bytes_to_copy = strlen(state_info);
    if (len < bytes_to_copy)
        return -EFAULT;
    
    if (copy_to_user(buffer, state_info, bytes_to_copy))
        return -EFAULT;
    
    *offset = bytes_to_copy;
    return bytes_to_copy;
}

static ssize_t vled_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    struct vled_device_data *dev_data = filep->private_data;
    char cmd[256];
    
    if (len > 255)
        return -EINVAL;
    
    if (copy_from_user(cmd, buffer, len))
        return -EFAULT;
    
    cmd[len] = '\0';
    
    mutex_lock(&dev_data->lock);
    
    // Обработка команд
    if (strncmp(cmd, "ON", 2) == 0) {
        dev_data->led_state = 1;
        printk(KERN_INFO "Virtual LED: Turned ON\n");
    } else if (strncmp(cmd, "OFF", 3) == 0) {
        dev_data->led_state = 0;
        printk(KERN_INFO "Virtual LED: Turned OFF\n");
    } else if (strncmp(cmd, "BRIGHTNESS ", 11) == 0) {
        int brightness;
        if (sscanf(cmd + 11, "%d", &brightness) == 1) {
            if (brightness >= 0 && brightness <= 255) {
                dev_data->brightness = brightness;
                printk(KERN_INFO "Virtual LED: Brightness set to %d\n", brightness);
            }
        }
    } else if (strncmp(cmd, "COLOR ", 6) == 0) {
        char color[16];
        if (sscanf(cmd + 6, "%15s", color) == 1) {
            strncpy(dev_data->color, color, sizeof(dev_data->color) - 1);
            dev_data->color[sizeof(dev_data->color) - 1] = '\0';
            printk(KERN_INFO "Virtual LED: Color set to %s\n", color);
        }
    }
    
    mutex_unlock(&dev_data->lock);
    return len;
}

// Операции файловых операций
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = vled_open,
    .read = vled_read,
    .write = vled_write,
    .release = vled_release,
};

// Функции для sysfs атрибутов
static ssize_t led_state_show(struct device *dev, 
                             struct device_attribute *attr, 
                             char *buf)
{
    return sprintf(buf, "%d\n", device_data.led_state);
}

static ssize_t led_state_store(struct device *dev,
                              struct device_attribute *attr,
                              const char *buf, size_t count)
{
    int state;
    if (sscanf(buf, "%d", &state) == 1) {
        if (state == 0 || state == 1) {
            mutex_lock(&device_data.lock);
            device_data.led_state = state;
            mutex_unlock(&device_data.lock);
            printk(KERN_INFO "Virtual LED: State changed to %d via sysfs\n", state);
        }
    }
    return count;
}

static ssize_t brightness_show(struct device *dev,
                              struct device_attribute *attr,
                              char *buf)
{
    return sprintf(buf, "%d\n", device_data.brightness);
}

static ssize_t brightness_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count)
{
    int brightness;
    if (sscanf(buf, "%d", &brightness) == 1) {
        if (brightness >= 0 && brightness <= 255) {
            mutex_lock(&device_data.lock);
            device_data.brightness = brightness;
            mutex_unlock(&device_data.lock);
            printk(KERN_INFO "Virtual LED: Brightness changed to %d via sysfs\n", brightness);
        }
    }
    return count;
}

static ssize_t color_show(struct device *dev,
                         struct device_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%s\n", device_data.color);
}

static ssize_t color_store(struct device *dev,
                          struct device_attribute *attr,
                          const char *buf, size_t count)
{
    char new_color[16];
    if (sscanf(buf, "%15s", new_color) == 1) {
        mutex_lock(&device_data.lock);
        strncpy(device_data.color, new_color, sizeof(device_data.color) - 1);
        device_data.color[sizeof(device_data.color) - 1] = '\0';
        mutex_unlock(&device_data.lock);
        printk(KERN_INFO "Virtual LED: Color changed to %s via sysfs\n", new_color);
    }
    return count;
}

// Определение sysfs атрибутов
static DEVICE_ATTR(led_state, 0664, led_state_show, led_state_store);
static DEVICE_ATTR(brightness, 0664, brightness_show, brightness_store);
static DEVICE_ATTR(color, 0664, color_show, color_store);

static struct attribute *vled_attrs[] = {
    &dev_attr_led_state.attr,
    &dev_attr_brightness.attr,
    &dev_attr_color.attr,
    NULL,
};

static struct attribute_group vled_attr_group = {
    .attrs = vled_attrs,
};

// Инициализация устройства
static int __init vled_init(void)
{
    dev_t dev_num;
    int retval;
    
    printk(KERN_INFO "Virtual LED Driver v2.2: Initializing...\n");
    
    // Инициализация данных устройства
    mutex_init(&device_data.lock);
    device_data.led_state = 0;
    device_data.brightness = 128;
    strcpy(device_data.color, "green");
    
    // Динамическое выделение major номера
    retval = alloc_chrdev_region(&dev_num, 0, MAX_DEVICES, DEVICE_NAME);
    if (retval < 0) {
        printk(KERN_ALERT "Failed to allocate character device region\n");
        return retval;
    }
    
    major_number = MAJOR(dev_num);
    printk(KERN_INFO "Virtual LED Driver: Registered with major number %d\n", major_number);
    
    // Создание класса устройства - совместимость с новыми версиями ядра
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    vled_class = class_create(CLASS_NAME);
#else
    vled_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(vled_class)) {
        unregister_chrdev_region(dev_num, MAX_DEVICES);
        printk(KERN_ALERT "Failed to create device class\n");
        return PTR_ERR(vled_class);
    }
    
    // Создание устройства
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    vled_device = device_create(vled_class, NULL, dev_num, NULL, "%s", DEVICE_NAME);
#else
    vled_device = device_create(vled_class, NULL, dev_num, NULL, DEVICE_NAME);
#endif
    if (IS_ERR(vled_device)) {
        class_destroy(vled_class);
        unregister_chrdev_region(dev_num, MAX_DEVICES);
        printk(KERN_ALERT "Failed to create device\n");
        return PTR_ERR(vled_device);
    }
    
    // Создание sysfs атрибутов
    retval = sysfs_create_group(&vled_device->kobj, &vled_attr_group);
    if (retval) {
        device_destroy(vled_class, dev_num);
        class_destroy(vled_class);
        unregister_chrdev_region(dev_num, MAX_DEVICES);
        printk(KERN_ALERT "Failed to create sysfs group\n");
        return retval;
    }
    
    // Инициализация cdev
    cdev_init(&vled_cdev, &fops);
    vled_cdev.owner = THIS_MODULE;
    
    // Добавление cdev в систему
    retval = cdev_add(&vled_cdev, dev_num, MAX_DEVICES);
    if (retval) {
        sysfs_remove_group(&vled_device->kobj, &vled_attr_group);
        device_destroy(vled_class, dev_num);
        class_destroy(vled_class);
        unregister_chrdev_region(dev_num, MAX_DEVICES);
        printk(KERN_ALERT "Failed to add character device\n");
        return retval;
    }
    
    printk(KERN_INFO "Virtual LED Driver: Successfully initialized\n");
    printk(KERN_INFO "Device node: /dev/%s\n", DEVICE_NAME);
    printk(KERN_INFO "Sysfs path: /sys/class/%s/%s/\n", CLASS_NAME, DEVICE_NAME);
    printk(KERN_INFO "Kernel version: %u (6.12.48)\n", LINUX_VERSION_CODE);
    
    return 0;
}

static void __exit vled_exit(void)
{
    dev_t dev_num = MKDEV(major_number, 0);
    
    printk(KERN_INFO "Virtual LED Driver: Exiting...\n");
    
    // Удаление sysfs атрибутов
    sysfs_remove_group(&vled_device->kobj, &vled_attr_group);
    
    // Удаление cdev
    cdev_del(&vled_cdev);
    
    // Удаление устройства
    device_destroy(vled_class, dev_num);
    
    // Удаление класса
    class_destroy(vled_class);
    
    // Освобождение номера устройства
    unregister_chrdev_region(dev_num, MAX_DEVICES);
    
    mutex_destroy(&device_data.lock);
    
    printk(KERN_INFO "Virtual LED Driver: Successfully unloaded\n");
}

module_init(vled_init);
module_exit(vled_exit);