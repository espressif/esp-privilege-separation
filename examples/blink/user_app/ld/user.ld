#include "user_config.h"

ENTRY(_user_main);

MEMORY
{
    iram_seg (RX) : org = 0x40390000,
                len = 0x10000

    dram_seg (RW) : org = 0x3FCC0000,
                len = 0x20000

    drom_seg (R) : org = 0x3C400020,
               len = 0x400000 - 0x20

    flash_rom_seg (RX) : org = 0x42400020,
                len = 0x400000 - 0x20
}

SECTIONS
{
  .uiram :
  {
    _user_text_start = ABSOLUTE(.);
    *(.init.literal)
    *(.init)
    *( .uiram  .uiram.*)
    *( .iram*)

#if CONFIG_USER_CODE_IN_IRAM
    *(.text .text.* .literal .literal.*)
#endif

    *(.fini.literal)
    *(.fini)
    _user_text_end = ABSOLUTE(.);
  } > iram_seg

  ASSERT(((_user_text_end - ORIGIN(iram_seg)) <= LENGTH(iram_seg)), "User IRAM does not fit.")

  .data :
  {
    _user_data_start = ABSOLUTE(.);
    *( .data  .data.*  .udram  .udram.*)
    _user_data_end = ABSOLUTE(.);

    _user_bss_start = ABSOLUTE(.);

    *( .bss  .bss.*  COMMON .sbss .sbss.*)

    . = ALIGN (8);
    _user_bss_end = ABSOLUTE(.);
    /* The heap starts right after end of this section */
    _user_heap_start = ABSOLUTE(.);
  } > dram_seg

  ASSERT(((_user_bss_end - ORIGIN(dram_seg)) <= LENGTH(dram_seg)), "User DRAM does not fit.")

  .flash_text :
  {
    _user_xip_text_start = ABSOLUTE(.);

#if !CONFIG_USER_CODE_IN_IRAM
    *(.text .text.* .literal .literal.*)
#endif

    _user_xip_text_end = ABSOLUTE(.);
    _user_pid_xip_end = ABSOLUTE(.);
   } > flash_rom_seg

  .flash_rodata_dummy (NOLOAD):
  {
    . = SIZEOF(.flash_text);
    . = ALIGN(0x10000) + 0x20;
    _rodata_reserved_start = .;
  } > drom_seg

  .flash_rodata :
  {
    _user_rodata_start = ABSOLUTE(.);

    *(.rodata_desc .rodata_desc.*)               /* Should be the first.  App version info.        DO NOT PUT ANYTHING BEFORE IT! */
    *(.rodata_custom_desc .rodata_custom_desc.*) /* Should be the second. Custom app version info. DO NOT PUT ANYTHING BEFORE IT! */

    *(.rodata)
    *(.rodata.*)
    *(.dynamic)
    _user_rodata_end = ABSOLUTE(.);

    /* Literals are also RO data. */
    _lit4_start = ABSOLUTE(.);
    *(*.lit4)
    *(.lit4.*)
    _lit4_end = ABSOLUTE(.);

    . = ALIGN(4);
  } > drom_seg

}
