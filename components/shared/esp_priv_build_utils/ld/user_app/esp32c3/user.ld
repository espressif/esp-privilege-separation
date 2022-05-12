#include "user_config.h"
#include "memory_layout.h"
#include "sdkconfig.h"

#define IRAM_START          0x40380000
#define RTC_END             0x52000000
#define I_D_MEMORY_OFFSET   0x700000

ENTRY(_user_main);

MEMORY
{
    iram_seg (RX) : org = USER_IRAM_START,
                len = USER_IRAM_END - USER_IRAM_START

    dram_seg (RW) : org = USER_DRAM_START,
                len = USER_DRAM_END - USER_DRAM_START

    drom_seg (R) : org = 0x3C400020,
               len = 0x400000 - 0x20

    flash_rom_seg (RX) : org = 0x42400020,
                len = 0x400000 - 0x20
}

SECTIONS
{
  .iram0.text :
  {
    _iram_start = ABSOLUTE(IRAM_START);
    _user_text_start = ABSOLUTE(.);
    _reserve_w1_iram_start = ABSOLUTE(.);
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
    _reserve_w1_iram_end = ORIGIN(iram_seg) + LENGTH(iram_seg);
    /* _iram_end is used to determine the reserved area while registering heap
     * We need to reserve memory till the start of user space data segment
     */
    _iram_end = ORIGIN(dram_seg) + I_D_MEMORY_OFFSET;
  } > iram_seg

  ASSERT(((_user_text_end - ORIGIN(iram_seg)) <= LENGTH(iram_seg)), "User IRAM does not fit.")

  .dram0.data :
  {
    _user_data_start = ABSOLUTE(.);
    _data_start = ABSOLUTE(.);
    _reserve_w1_dram_start = ABSOLUTE(.);
    *( .sdata.*)
    *( .sdata2.*)
    *( .data  .data.*  .udram  .udram.*)
    _user_data_end = ABSOLUTE(.);
  } > dram_seg

  .dram0.bss (NOLOAD) :
  {
    _user_bss_start = ABSOLUTE(.);

    *( .bss  .bss.*  COMMON .sbss .sbss.*)

    . = ALIGN (8);
    _user_bss_end = ABSOLUTE(.);
    /* The heap starts right after end of this section */
    _user_heap_start = ABSOLUTE(.);
    /* Protected app spawns the first user space task and it requires user space stack for that.
     * We reserve the starting bytes which is used as a stack and errno variable
     *
     * _heap_start is used by the heap allocator to determine the start of the heap
     */
    KEEP (*(.startup_resources))

    . = ALIGN (16);
    _heap_start = ABSOLUTE(.);
    _reserve_w1_dram_end = ORIGIN(dram_seg) + LENGTH(dram_seg);
  } > dram_seg

  ASSERT(((_user_bss_end - ORIGIN(dram_seg)) <= LENGTH(dram_seg)), "User DRAM does not fit.")

  .flash.text :
  {
    _user_xip_text_start = ABSOLUTE(.);

#if !CONFIG_USER_CODE_IN_IRAM
    *(.text .text.* .literal .literal.*)
#endif

    _user_xip_text_end = ABSOLUTE(.);
    _user_pid_xip_end = ABSOLUTE(.);
   } > flash_rom_seg

  .flash_rodata_dummy (NOLOAD): ALIGN(0x10)
  {
    . = SIZEOF(.flash.text);
    . = ALIGN(0x10000) + 0x20;
    _rodata_reserved_start = .;
  } > drom_seg

  .flash.rodata : ALIGN(0x10)
  {
    _user_rodata_start = ABSOLUTE(.);

    *(.rodata_desc .rodata_desc.*)               /* Should be the first.  App version info.        DO NOT PUT ANYTHING BEFORE IT! */
    *(.rodata_custom_desc .rodata_custom_desc.*) /* Should be the second. Custom app version info. DO NOT PUT ANYTHING BEFORE IT! */

    *(.rodata)
    *(.rodata.*)
    *(.srodata.*)
    *(.dynamic)
    _user_rodata_end = ABSOLUTE(.);

    . = ALIGN(4);
    /* Addresses of memory regions reserved via SOC_RESERVE_MEMORY_REGION() */
    soc_reserved_memory_region_start = ABSOLUTE(.);
    KEEP (*(.reserved_memory_address))
    soc_reserved_memory_region_end = ABSOLUTE(.);

    /* Literals are also RO data. */
    _lit4_start = ABSOLUTE(.);
    *(*.lit4)
    *(.lit4.*)
    _lit4_end = ABSOLUTE(.);

    . = ALIGN(4);
    _rtc_force_slow_end = ABSOLUTE(RTC_END);
  } > drom_seg

}
