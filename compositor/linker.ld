OUTPUT_FORMAT(elf64-x86-64)
ENTRY(compositor_entry)

PHDRS
{
    text   PT_LOAD FLAGS(0x05);
    rodata PT_LOAD FLAGS(0x04);
    data   PT_LOAD FLAGS(0x06);
}

SECTIONS
{
    . = 0xffffffffe2000000;
    .text : { *(.text .text.*) } :text
    . = ALIGN(0x1000);
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
    __compositor_image_end = .;
    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}
