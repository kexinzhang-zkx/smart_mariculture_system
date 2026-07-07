#!/bin/bash
export PATH=/usr/local/arm/5.4.0/bin:$PATH
cd /home/huangsimin09/lv_port_linux_sdl_gec6818
make clean
make --file=Make_arm -j12
