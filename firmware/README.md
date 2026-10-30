# 主表 / 从表固件

同一仓库构建两套 ESP32-S3 固件，使用同一份 `partitions.csv`（16 MB Flash）。

| 目标 | 硬件 | 构建目录 | 配置 | 应用文件 |
|---|---|---|---|---|
| master | ESP32-S3-Touch-LCD-1.85B，360×360 | `build-master/` | `sdkconfig.master` | `obd_brz_gauge_master.bin` |
| slave | ESP32-S3-LCD-1.85，360×360 | `build-slave/` | `sdkconfig.slave` | `obd_brz_gauge_slave.bin` |

左右从表使用同一份 slave 固件，接线决定左右身份。

## 构建与打包

先激活 ESP-IDF v5.5.3 环境，在仓库根目录执行：

```sh
./tools/build_gauge_firmware.sh master
./tools/build_gauge_firmware.sh slave
python3 tools/gen_release.py
```

也可执行 `./tools/release.sh` 一次完成两套构建和本地打包。脚本不会提交、推送或烧录。

共享配置为 `sdkconfig.defaults`，目标配置分别为 `sdkconfig.defaults.master` 和
`sdkconfig.defaults.slave`。不要使用旧的 `build/` 或根目录 `sdkconfig` 构建。

输出目录：

```text
firmware/release/
├── latest.json                  # 兼容现有主表 OTA 客户端，文件路径指向 master/
├── master/
│   ├── obd_brz_gauge_master.bin
│   ├── bootloader/bootloader.bin
│   ├── partition_table/partition-table.bin
│   ├── ota_data_initial.bin
│   ├── bootmedia.bin
│   ├── vehicle_fuel_tanks.json
│   ├── latest.json
│   ├── flash_args
│   └── flash_address_map.txt
└── slave/
    ├── obd_brz_gauge_slave.bin
    ├── bootloader/bootloader.bin
    ├── partition_table/partition-table.bin
    ├── ota_data_initial.bin
    ├── bootmedia.bin
    ├── latest.json
    ├── flash_args
    └── flash_address_map.txt
```

打包器校验目标类型、所需文件和两套构建的版本信息；校验通过后替换旧包。
清单记录各文件大小和 SHA256，版本信息来自构建目录，不从当前 Git HEAD 推测。
`count` 随提交递增，未提交修改仍沿用同一计数；对外发布前应先提交源码再重新构建。
根目录和各目标目录的 `latest.json` 中，文件路径均相对于对应清单所在目录。
从表保留同样的分区布局和 bootmedia 镜像，但运行时不启动媒体服务。

## 烧录

从仓库根目录进入对应目标目录，在其内部执行参数文件：

```sh
cd firmware/release/master
python -m esptool --chip esp32s3 --port PORT -b 460800 \
  --before default_reset --after hard_reset write_flash @flash_args
```

从表改为 `firmware/release/slave`。将 `PORT` 替换为实际串口。

| 地址 | 内容 |
|---|---|
| `0x0` | 对应目标 Bootloader |
| `0x8000` | 分区表 |
| `0xF000` | OTA 初始数据 |
| `0x20000` | 对应目标应用 |
| `0x620000` | 主题分区，不由此命令写入 |
| `0xA20000` | 开机动画原始分区镜像 |

实际参数从各构建目录的 `flasher_args.json` 生成。`write_flash` 只擦写指定区域，
不擦除整个芯片，不主动清除 NVS 和主题；OTA 初始数据会将启动槽位设为 ota_0。

开机动画源文件位于 `bootmedia/slot_a/`，由 CMake 生成各构建目录的 `bootmedia.bin`。
