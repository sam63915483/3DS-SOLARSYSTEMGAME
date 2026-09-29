#!/bin/bash
# Packages the built app as sk1.cia (installable with FBI). Run from MSYS2 bash after `make`:
#   C:\msys64\usr\bin\bash.exe -lc "source /etc/profile; cd /c/3ds/app && make && bash build_cia.sh"
set -e
cd "$(dirname "$0")"
TOOLS=../tools
BANNERTOOL=$(ls $TOOLS/bannertool/*/bannertool.exe | head -1)
MAKEROM=$TOOLS/makerom/makerom.exe

mkdir -p cia/romfs
touch cia/romfs/.keep

"$BANNERTOOL" makebanner -i cia/banner.png -a cia/banner.wav -o cia/banner.bnr
"$BANNERTOOL" makesmdh -s "SK-1 Clone" -l "SK-1 style sampling keyboard" -p "Sam" -i cia/icon.png -o cia/icon.icn

"$MAKEROM" -f cia -o sk1.cia -elf sk1.elf -rsf ../tools/template.rsf \
  -icon cia/icon.icn -banner cia/banner.bnr -exefslogo -target t \
  -DAPP_TITLE="SK-1 Clone" -DAPP_PRODUCT_CODE=CTR-P-SK1C -DAPP_CATEGORY=Application \
  -DAPP_UNIQUE_ID=0xF5A1C -DAPP_USE_ON_SD=true -DAPP_ENCRYPTED=false -DAPP_ROMFS=cia/romfs \
  -DAPP_MEMORY_TYPE=Application -DAPP_SYSTEM_MODE=64MB -DAPP_SYSTEM_MODE_EXT=Legacy \
  -DAPP_CPU_SPEED=268MHz -DAPP_ENABLE_L2_CACHE=false -DAPP_VERSION_MAJOR=1

ls -la sk1.cia
