#include "pebble_app_info.h"
#include "src/resource_ids.auto.h"

const PebbleAppInfo __pbl_app_info __attribute__ ((section (".pbl_header"))) = {
  .header = "PBLAPP",
  .struct_version = { APP_INFO_CURRENT_STRUCT_VERSION_MAJOR, APP_INFO_CURRENT_STRUCT_VERSION_MINOR },
  .sdk_version = { APP_INFO_CURRENT_SDK_VERSION_MAJOR, APP_INFO_CURRENT_SDK_VERSION_MINOR },
  .app_version = { 0, 2 },
  .load_size = 0xb6b6,
  .offset = 0xb6b6b6b6,
  .crc = 0xb6b6b6b6,
  .name = "Countdown",
  .company = "Don Krause",
  .icon_resource_id = RESOURCE_ID_ICON_IMAGE,
  .sym_table_addr = 0xA7A7A7A7,
  .flags = 0,
  .num_reloc_entries = 0xdeadcafe,
  .uuid = { 0x0D, 0xFE, 0xEB, 0x6B, 0x17, 0x9A, 0x4B, 0x5C, 0x9F, 0x00, 0xAA, 0xD6, 0xB8, 0x1E, 0x8E, 0xB3 },
  .virtual_size = 0xb6b6
};
