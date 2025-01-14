#!/bin/bash

cd build

esptool.py --chip esp32 merge_bin -o wican-rejsa-v3_4-20250113.bin --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0 bootloader/bootloader.bin 0x10000 wican-fw_obd_v402-17-g9d89f6d-dirty.bin 0x8000 partition_table/partition-table.bin 0xd000 ota_data_initial.bin 
