# Makefile for ofono-server - Cross compile for aarch64-linux-gnu

CC = aarch64-linux-gnu-gcc
# 移除 -DDISABLE_PRINTF 以启用调试输出
# 添加 -DDISABLE_PRINTF 禁用所有printf输出
CFLAGS = -Wall -O2 -g -DDISABLE_PRINTF -DMG_ENABLE_LINES=0 -include debug.h

# GLib 库路径
GLIB_DIR = ..
INCLUDES = -I$(GLIB_DIR)/include/glib-2.0 \
           -I$(GLIB_DIR)/lib/glib-2.0/include \
           -I$(GLIB_DIR)/include/gio-unix-2.0 \
           -I. -Iinclude -Iinclude/system -Iinclude/handlers -Iinclude/lib

LDFLAGS = -L$(GLIB_DIR)/lib -Wl,-rpath-link,$(GLIB_DIR)/lib -Wl,--allow-shlib-undefined
LIBS = -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lgmodule-2.0 -lpthread

BUILD_DIR = build
TARGET = $(BUILD_DIR)/ofono-server

# 源文件分类
MAIN_SRCS = main.c mongoose.c packed_fs.c
HANDLER_SRCS = handlers/http_server.c handlers/handlers.c
SYSTEM_SRCS = system/sysinfo.c system/modem.c system/airplane.c system/ofono.c \
              system/exec_utils.c system/advanced.c \
              system/traffic.c system/reboot.c system/charge.c system/sms.c system/update.c \
              system/usb_mode.c system/plugin.c system/plugin_storage.c \
              system/sha256.c system/auth.c system/database.c system/apn.c system/json_builder.c \
              system/netif.c
SRCS = $(MAIN_SRCS) $(HANDLER_SRCS) $(SYSTEM_SRCS)
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/mongoose.o $(BUILD_DIR)/packed_fs.o \
       $(BUILD_DIR)/http_server.o $(BUILD_DIR)/handlers.o \
       $(BUILD_DIR)/sysinfo.o $(BUILD_DIR)/modem.o $(BUILD_DIR)/airplane.o \
       $(BUILD_DIR)/ofono.o $(BUILD_DIR)/exec_utils.o \
       $(BUILD_DIR)/advanced.o $(BUILD_DIR)/traffic.o $(BUILD_DIR)/reboot.o \
       $(BUILD_DIR)/charge.o $(BUILD_DIR)/sms.o $(BUILD_DIR)/update.o $(BUILD_DIR)/usb_mode.o \
       $(BUILD_DIR)/plugin.o $(BUILD_DIR)/plugin_storage.o \
       $(BUILD_DIR)/sha256.o $(BUILD_DIR)/auth.o $(BUILD_DIR)/database.o $(BUILD_DIR)/apn.o \
       $(BUILD_DIR)/json_builder.o $(BUILD_DIR)/netif.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	aarch64-linux-gnu-strip $@
	@echo "Build complete: $@"

# 主目录源文件
$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/mongoose.o: mongoose.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/packed_fs.o: packed_fs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# handlers 目录
$(BUILD_DIR)/http_server.o: handlers/http_server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/handlers.o: handlers/handlers.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# system 目录
$(BUILD_DIR)/sysinfo.o: system/sysinfo.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/modem.o: system/modem.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/airplane.o: system/airplane.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/ofono.o: system/ofono.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/exec_utils.o: system/exec_utils.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/advanced.o: system/advanced.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/traffic.o: system/traffic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/reboot.o: system/reboot.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/charge.o: system/charge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/sms.o: system/sms.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/update.o: system/update.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/usb_mode.o: system/usb_mode.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/plugin.o: system/plugin.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/plugin_storage.o: system/plugin_storage.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/sha256.o: system/sha256.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/auth.o: system/auth.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/database.o: system/database.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/apn.o: system/apn.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/json_builder.o: system/json_builder.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/netif.o: system/netif.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR):
ifeq ($(OS),Windows_NT)
	if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
else
	mkdir -p $(BUILD_DIR)
endif

clean:
ifeq ($(OS),Windows_NT)
	if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
else
	rm -rf $(BUILD_DIR)
endif
