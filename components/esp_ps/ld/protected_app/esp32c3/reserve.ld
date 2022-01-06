#include "sdkconfig.h"

SECTIONS
{
    .reserve_w1_iram (NOLOAD) :
    {
        /* W1 IRAM should be placed after W0 IRAM, _iram_bss_end is the end of the W0 IRAM region
         * 16 bytes padding is required between W0 and W1 as CPU tries to pre-fetch instructions
         */
        . = ABSOLUTE(_iram_bss_end) + 16;
        /* C3 PMS controller has a granularity of 512 bytes */
        . = ALIGN (0x200);
        _reserve_w1_iram_start = ABSOLUTE(.);
        . += CONFIG_PS_ALLOCATE_USER_IRAM_SIZE;
        . += 16;
        . = ALIGN (0x200);
        _reserve_w1_iram_end = ABSOLUTE(.);
        _iram_end = ABSOLUTE(.);
    } > iram0_0_seg

    .reserve_w1_dram (NOLOAD) :
    {
        /* Assign W1 DRAM start address as per the size configured in Kconfig.
         * 0x3FCDF060 - 0x3FCE0000 space is reserved for ROM code so to avoid
         * overlapping for reserved memory regions, treat 0x3FCDF060 as the end of DRAM region
         */
#if CONFIG_PS_MEMORY_ALLOCATE_PROTECTED
        _reserve_w1_dram_start = ABSOLUTE(_data_start + CONFIG_PS_ALLOCATE_DRAM_SIZE);
#elif CONFIG_PS_MEMORY_ALLOCATE_USER
        _reserve_w1_dram_start = ABSOLUTE(0x3FCDF060 - CONFIG_PS_ALLOCATE_DRAM_SIZE);
#endif
        /* Align the starting boundary to 512 bytes as it is a requirement for the PMS split line */
        _reserve_w1_dram_start = ABSOLUTE((_reserve_w1_dram_start + 0x200 - 1) & ~(0x200 - 1));
        _reserve_w1_dram_end = ABSOLUTE(0x3FCDF060);
    } > dram0_0_seg

  ASSERT((_heap_start <= _reserve_w1_dram_start),
          "Protected DRAM segment contents does not fit.")
}
