#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define DEVICE_PATH "/dev/vled"
#define SYSFS_STATE "/sys/class/vled/vled/led_state"
#define SYSFS_BRIGHTNESS "/sys/class/vled/vled/brightness"
#define SYSFS_COLOR "/sys/class/vled/vled/color"

void print_state(const char *label)
{
    printf("\n%s\n", label);
    printf("========================================\n");
    
    // Чтение через sysfs
    char buffer[256];
    FILE *fp;
    
    fp = fopen(SYSFS_STATE, "r");
    if (fp) {
        fgets(buffer, sizeof(buffer), fp);
        printf("State: %s", buffer);
        fclose(fp);
    }
    
    fp = fopen(SYSFS_BRIGHTNESS, "r");
    if (fp) {
        fgets(buffer, sizeof(buffer), fp);
        printf("Brightness: %s", buffer);
        fclose(fp);
    }
    
    fp = fopen(SYSFS_COLOR, "r");
    if (fp) {
        fgets(buffer, sizeof(buffer), fp);
        printf("Color: %s", buffer);
        fclose(fp);
    }
    
    // Чтение через устройство
    int fd = open(DEVICE_PATH, O_RDONLY);
    if (fd >= 0) {
        printf("Device output:\n");
        int bytes = read(fd, buffer, sizeof(buffer)-1);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            printf("%s", buffer);
        }
        close(fd);
    }
}

void write_command(const char *command)
{
    int fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        printf("Error opening device: %s\n", strerror(errno));
        return;
    }
    
    write(fd, command, strlen(command));
    close(fd);
    printf("Command executed: %s\n", command);
}

void write_sysfs(const char *path, const char *value)
{
    FILE *fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%s", value);
        fclose(fp);
        printf("Sysfs write: %s = %s\n", path, value);
    } else {
        printf("Error writing to %s: %s\n", path, strerror(errno));
    }
}

int main()
{
    printf("Virtual LED Driver Test Program\n");
    printf("===============================\n");
    
    // Проверка существования драйвера
    if (access(DEVICE_PATH, F_OK) != 0) {
        printf("ERROR: Driver not loaded!\n");
        printf("Please load the driver first:\n");
        printf("  sudo insmod virtual_led_driver.ko\n");
        return 1;
    }
    
    printf("Driver found. Starting tests...\n");
    
    // Тест 1: Начальное состояние
    print_state("1. Initial State");
    
    // Тест 2: Включение через устройство
    printf("\n\n2. Turning LED ON via device");
    write_command("ON");
    sleep(1);
    print_state("After turning ON");
    
    // Тест 3: Изменение яркости через устройство
    printf("\n\n3. Setting brightness to 200 via device");
    write_command("BRIGHTNESS 200");
    sleep(1);
    print_state("After brightness change");
    
    // Тест 4: Изменение цвета через устройство
    printf("\n\n4. Setting color to blue via device");
    write_command("COLOR blue");
    sleep(1);
    print_state("After color change");
    
    // Тест 5: Выключение через sysfs
    printf("\n\n5. Turning LED OFF via sysfs");
    write_sysfs(SYSFS_STATE, "0");
    sleep(1);
    print_state("After turning OFF via sysfs");
    
    // Тест 6: Изменение яркости через sysfs
    printf("\n\n6. Setting brightness to 100 via sysfs");
    write_sysfs(SYSFS_BRIGHTNESS, "100");
    sleep(1);
    print_state("After sysfs brightness change");
    
    // Тест 7: Изменение цвета через sysfs
    printf("\n\n7. Setting color to red via sysfs");
    write_sysfs(SYSFS_COLOR, "red");
    sleep(1);
    print_state("After sysfs color change");
    
    // Тест 8: Включение через устройство
    printf("\n\n8. Turning LED ON via device again");
    write_command("ON");
    sleep(1);
    print_state("Final state");
    
    printf("\n\nAll tests completed successfully!\n");
    printf("\nYou can also test manually:\n");
    printf("  echo 'ON' > /dev/vled\n");
    printf("  echo '1' > /sys/class/vled/vled/led_state\n");
    printf("  cat /dev/vled\n");
    
    return 0;
}