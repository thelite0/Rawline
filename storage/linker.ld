OUTPUT_FORMAT(elf64-x86-64)
ENTRY(storage_entry)
PHDRS { text PT_LOAD FLAGS(0x05); rodata PT_LOAD FLAGS(0x04); data PT_LOAD FLAGS(0x06); }
SECTIONS {
    . = 0xffffffffe5000000;
    .text : { *(.text .text.*) } :text
    . = ALIGN(0x1000);
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}
