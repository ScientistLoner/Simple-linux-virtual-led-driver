obj-m := virtual_led_driver.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

CC := gcc
CFLAGS := -Wall -Wextra -g
GTKFLAGS := `pkg-config --cflags --libs gtk+-3.0`

all: driver gui test

driver:
	@echo "Building driver..."
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	@echo "Driver built successfully"

virtual_led_driver.ko: driver

gui_control: gui_control.c
	@echo "Building GUI application..."
	$(CC) $(CFLAGS) -o gui_control gui_control.c $(GTKFLAGS)
	@echo "GUI application built successfully"

test_control: test_control.c
	@echo "Building test application..."
	$(CC) $(CFLAGS) -o test_control test_control.c
	@echo "Test application built successfully"

gui: gui_control

test: test_control

clean:
	@echo "Cleaning..."
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f gui_control test_control
	rm -f *.o *.ko *.mod.c modules.order Module.symvers .*.cmd
	rm -rf .tmp_versions
	@echo "Clean complete"

install: virtual_led_driver.ko
	@echo "Installing driver..."
	@if lsmod | grep -q virtual_led; then \
		echo "Driver is already loaded. Removing first..."; \
		sudo rmmod virtual_led_driver; \
	fi
	sudo insmod virtual_led_driver.ko
	@echo "Driver installed successfully"
	@echo "Device node: /dev/vled"
	@echo "Sysfs path: /sys/class/vled/vled/"
	@sudo chmod 666 /dev/vled 2>/dev/null || true
	@echo "Permissions set for /dev/vled"

uninstall:
	@echo "Uninstalling driver..."
	@if lsmod | grep -q virtual_led; then \
		sudo rmmod virtual_led_driver; \
		echo "Driver uninstalled"; \
	else \
		echo "Driver is not loaded"; \
	fi

reinstall: uninstall clean all install

load: install

unload: uninstall

status:
	@echo "=== Virtual LED Driver Status ==="
	@echo ""
	@echo "Kernel Module:"
	@if lsmod | grep -q virtual_led; then \
		echo "  Loaded: YES"; \
		echo "  Module name: virtual_led_driver"; \
	else \
		echo "  Loaded: NO"; \
	fi
	@echo ""
	@echo "Device Files:"
	@if [ -e /dev/vled ]; then \
		echo "  /dev/vled exists"; \
		ls -la /dev/vled; \
		echo "  Permissions: $$(stat -c "%A %U %G" /dev/vled 2>/dev/null || echo "unknown")"; \
	else \
		echo "  /dev/vled not found"; \
	fi
	@echo ""
	@echo "Sysfs Interface:"
	@if [ -d /sys/class/vled ]; then \
		echo "  /sys/class/vled exists"; \
		ls -la /sys/class/vled/; \
		echo ""; \
		echo "Current State:"; \
		cat /sys/class/vled/vled/led_state 2>/dev/null | xargs echo "  State:"; \
		cat /sys/class/vled/vled/brightness 2>/dev/null | xargs echo "  Brightness:"; \
		cat /sys/class/vled/vled/color 2>/dev/null | xargs echo "  Color:"; \
	else \
		echo "  /sys/class/vled not found"; \
	fi
	@echo ""
	@echo "=== Last Kernel Messages ==="
	@dmesg | tail -10 | grep -i "virtual\|vled" || echo "  No recent messages"

debug:
	@echo "Clearing kernel messages..."
	sudo dmesg -C
	@echo "Loading driver..."
	sudo insmod virtual_led_driver.ko
	@echo "Driver messages:"
	dmesg

test-device:
	@echo "Testing device access..."
	@if [ -e /dev/vled ]; then \
		echo "Testing write..."; \
		echo "ON" | sudo tee /dev/vled >/dev/null; \
		echo "Testing read..."; \
		sudo cat /dev/vled; \
		echo "Testing sysfs..."; \
		cat /sys/class/vled/vled/* 2>/dev/null || echo "Sysfs not accessible"; \
	else \
		echo "Device not found"; \
	fi

help:
	@echo "Available commands:"
	@echo "  make all          - Build everything"
	@echo "  make driver       - Build only driver"
	@echo "  make gui          - Build GUI application"
	@echo "  make test         - Build test application"
	@echo "  make install      - Install/load driver"
	@echo "  make uninstall    - Uninstall/unload driver"
	@echo "  make reinstall    - Reinstall driver (clean, build, install)"
	@echo "  make status       - Show driver status"
	@echo "  make debug        - Load driver and show debug messages"
	@echo "  make clean        - Clean all built files"
	@echo "  make test-device  - Test device functionality"

.PHONY: all driver gui test clean install uninstall reinstall load unload status debug test-device help