MEMORY
{
  world1_iram0_seg(RX):              org = 0x40390000, len = 0x10000

  world1_dram0_seg(RX):              org = 0x3FCC0000, len = 0x20000
}

SECTIONS
{
    .reserve_w1_iram :
    {
        . = ORIGIN(world1_iram0_seg);
        _reserve_w1_iram_start = ABSOLUTE(.);
        . += LENGTH(world1_iram0_seg);
        _reserve_w1_iram_end = ABSOLUTE(.);
        _iram_end = ABSOLUTE(.);
    } > iram0_0_seg

  ASSERT((_iram_bss_end <= ORIGIN(world1_iram0_seg)),
          "Protected IRAM segment contents does not fit.")

  ASSERT((_bss_end <= ORIGIN(world1_dram0_seg)),
          "Protected DRAM segment contents does not fit.")

    .reserve_w1_dram :
    {
        _reserve_w1_dram_start = ORIGIN(world1_dram0_seg);
        _reserve_w1_dram_end = ORIGIN(world1_dram0_seg) + LENGTH(world1_dram0_seg);
    } > dram0_0_seg
}
