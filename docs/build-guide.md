# 构建与烧录指南

PaperColor 的编译在 **Docker 容器** 内完成，烧录使用 **主机 esptool**。

---

## 一、前提条件

| 工具 | 说明 |
|------|------|
| Docker | 运行 `espressif/idf:release-v5.5` 镜像 |
| esptool.py | 主机上安装：`pip install esptool` |
| USB 串口 | 设备通过 USB 连接到主机 |

---

## 二、完整构建与烧录流程

### 2.1 启动编译容器

```bash
docker run -d --rm --name papercolor-build \
  -v $(pwd):/workspaces/PaperColor \
  -w /workspaces/PaperColor \
  espressif/idf:release-v5.5 \
  sleep infinity
```

> 容器只需启动一次，后续编译复用即可。

### 2.2 编译

```bash
docker exec papercolor-build bash -c \
  ". /opt/esp/idf/export.sh > /dev/null 2>&1 && idf.py build"
```

编译产物生成在 `build/` 目录中：
- `build/bootloader/bootloader.bin` — 引导程序
- `build/partition_table/partition-table.bin` — 分区表
- `build/papercolor_template.bin` — 应用固件

### 2.3 确认设备端口

```bash
ls /dev/tty.* | grep -E 'usb|acm'
```

通常输出类似 `/dev/tty.usbmodem1101`。

### 2.4 烧录（主机 esptool）

```bash
cd build

esptool.py --chip esp32s3 \
  -p /dev/tty.usbmodem1101 \
  -b 460800 \
  --before default_reset --after hard_reset \
  write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0      bootloader/bootloader.bin \
  0x8000   partition_table/partition-table.bin \
  0x10000  papercolor_template.bin
```

### 2.5 监视串口输出

```bash
idf.py -p /dev/tty.usbmodem1101 monitor
# 或直接用:
python -m serial.tools.miniterm /dev/tty.usbmodem1101 115200
```

> 退出监视器：`Ctrl+]`

---

## 三、快速一键脚本

将以下内容保存为项目根目录的 `flash.sh`：

```bash
#!/bin/bash
PORT="${1:-/dev/tty.usbmodem1101}"
cd "$(dirname "$0")"

echo "=== Building in container ==="
docker exec papercolor-build bash -c \
  ". /opt/esp/idf/export.sh > /dev/null 2>&1 && idf.py build" || exit 1

echo ""
echo "=== Flashing to $PORT ==="
esptool.py --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0      build/bootloader/bootloader.bin \
  0x8000   build/partition_table/partition-table.bin \
  0x10000  build/papercolor_template.bin

echo ""
echo "=== Done ==="
```

用法：

```bash
chmod +x flash.sh
./flash.sh                              # 默认端口
./flash.sh /dev/tty.usbmodem1101        # 指定端口
```

---

## 四、仅编译（不烧录）

```bash
docker exec papercolor-build bash -c \
  ". /opt/esp/idf/export.sh > /dev/null 2>&1 && idf.py build"
```

---

## 五、清理构建

```bash
docker exec papercolor-build bash -c \
  ". /opt/esp/idf/export.sh > /dev/null 2>&1 && idf.py fullclean && idf.py build"
```

---

## 六、故障排查

| 问题 | 排查 |
|------|------|
| `idf.py: command not found` | 没有执行 `. /opt/esp/idf/export.sh` |
| 编译容器未运行 | `docker start papercolor-build` 或重新 `docker run` |
| 烧录时 `Failed to connect` | 检查 USB 连接、端口号、是否进入下载模式 |
| 烧录后无输出 | 检查串口监视器波特率（115200） |
| 烧录后反复重启 | 检查分区表是否匹配、flash 大小设置 |
