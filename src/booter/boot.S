/* Helpful constants for use in the bootloader. */
#include "boot.h"

	#=====================================================================
	# This is the wrapper that bridges the gap from the BIOS bootstrap to
	# our C program's entry-point.  It has two main responsibilities.
	# The first is to get our program off of the disk and into memory.
	# The second is to get from 16-bit real mode into 32-bit protected
	# mode.
	#=====================================================================

	# We need to output 16-bit x86 machine code at this point,
	# since the processor starts out in 16-bit real mode.
	.code16

	#=====================================================================
	# BOOTLOADER STARTS HERE
	#
	# This part of the code is responsible for loading the main program
	# off of the disk image and into memory at some available address.
	# We choose 0x00020000 (2000:0000 or equivalent), just because.  This
	# would give us hundreds of KB of memory to use, but we won't use
	# even a fraction of that space.
	# 
	# At this point, %dl will be the drive number of the boot device
	# (i.e. the disk that the bootloader # was loaded off of).
	# This allows us to load additional data from the boot disk.
	#=====================================================================

boot_start:
	# Initialize both data segment and stack segment to be 0, then
	# set up the top of the stack at 0000:f000.
	sub	%ax, %ax
	mov	%ax, %ds
	mov	%ax, %ss
	mov	$0xf000, %esp	# Use %esp since we'll be in 32-bit mode later

	# Hide the cursor, just to be "prettier."  This will carry over into
	# protected mode execution too.
	mov	$1, %ah
	mov	$0x2607, %cx	# Set cursor end-row to come before start-row
	int	$0x10

	mov	$STR_LOADING, %si
	call	print_str

	#---------------------------------------------------------------------
	# Load the remainder of the program into memory using int 13h
	# "Extended Read Operation."  If it fails, jump to boot_fail.
        
        # Use Extended Read Sectors to read data from disk
        movb    $0x42, %ah
        # Set drive index to point to first hard drive (at 0x80)
        movb    $0x80, %dl
        
        # Push DAP to the stack (in reverse order)
        # xxd floppy.img -> shows ELF starts at 0x200
        # (which is 512 in decimal, corresponding to index 1)
        pushl   $0              # high nibble of start index of read sectors
                                # in logical block addressing (LBA)
        pushl   $1              # low nibble of start index of read sectors
        # Buffer address (absolute memory location) is 0x00020000 (see boot.h)
        # Absolute memory location = 16 * segment + offset, so
        # segment = 0x2000 and offset = 0x0000 (concatenate them below)
        pushl   $0x20000000     # segment:offset of buffer to receive data
        # Number of sectors is number of sectors that are needed to contain
        # program.bin - e.g. if program.bin is 4.6KB, we need ~10 sectors
        pushw   $20             # number of 512-byte sectors to read
        # pushb   $0            # unused, set to 0
        # pushb   $0x10         # size of DAP (16 bytes)
	pushw	$0x10           # concatenation of above two bytes (commented)
        # Copy lower 16 bits of stack pointer to %si
        movw    %sp, %si        # segment:offset pointer to DAP
        # This is 16 bit and we set %ss = %ds = 0, so we can ignore it now

# nm boot.o reveals virtual addresses of these section headers (like read_data)
# and adding 0x7c00 to it allows you to break by address (e.g. break *0x7cbf)
read_data:
        int     $0x13           # Execute read data (sets carry flag if error)
        # Alternatively can simply move %esp but this is more readable
        popw	%cx
	popw    %cx
        popl    %ecx
        popl    %ecx
        popl    %ecx
        jc      boot_fail       # If error (carry flag), jump to boot_fail

	mov	$STR_LOADED, %si
	call	print_str

	#=====================================================================
	# Next, set up for transition from 16-bit real mode to 32-bit
	# protected mode.  All we strictly need for this is a Global
	# Descriptor Table, since we can leave paging off.  Then we can
	# switch over to protected mode.

start_protected:

	# Switch from real mode into protected mode
	# 1)  Disable interrupts, since any interrupt during this
	#     process would demolish us.  (We don't turn interrupts
	#     back on until we are safely in our C program, and have
	#     set up an Interrupt Descriptor Table.)
        cli # disable interrupts
	# 2)  Set the Global Descriptor Table Register (GDTR) to our
	#     simple Global Descriptor Table specified at the end of
	#     the boot-loader.  We are in 16-bit real mode.
	#     (We need a data32 prefix to ensure that all 32 bits of
	#     the GDT descriptor are loaded; default is to load only
	#     24 bits.  The CPU doesn't need an addr32 prefix but ELF
	#     doesn't do 16-bit relocations.)
        data32 addr32 lgdt gdtdesc # load GDTR with base address of GDT
	# 3)  Turn on protected mode by modifying Control Register cr0.
	#     See IA32 Manual Volume 3A, Section 2.5 for details of
	#     which bit(s) to set.  Don't turn on paging - we don't
	#     need it!  Also, you should not modify any other bits in
	#     cr0:  read it, OR in the appropriate bit(s), then write
	#     it back.
        movl    %cr0, %eax # read in cr0 control register
        orl     $CR0_PE, %eax # set bits to turn on protection mode        
        movl    %eax, %cr0 # enable protected mode

	# We're now in protected mode, but still in a 16-bit segment.  The
	# CPU still has the real-mode code segment cached in %cs's segment
	# descriptor.  We need to reload %cs, and the easiest way is to use a
	# far jump.
	#
	# Because we're not running in a 32-bit segment the data32 prefix is
	# needed to jump to a 32-bit offset in the target segment.
	data32 ljmp $SEL_CODESEG, $1f

	# We're now in protected mode in a 32-bit segment.  Tell the assembler.
	.code32

	# Reload all the other segment registers and the stack pointer to
	# point into our new GDT.
1:	mov $SEL_DATASEG, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

	#=====================================================================
	# Time to call into the C entry-point!  It should never return,
	# but if it does, we'll just halt.

call_c_entry:

	cld			# C code expects this...
	movl        $0, %ebp    # Null-terminate the program's backtrace
	
        # Call the entry-point of the C program!  The program is
	# compiled and linked into an ELF file, so the file's ELF
	# header will specify the entry-point into the program.
	# The e_entry field is a 32-bit value at a fixed offset
        # from the start of the fiel data.

        movl        $PROGRAM_BASE_ADDR, %eax # start of file (main program) data
        addl        $0x18, %eax # e_entry is at a fixed 0x18 offset from start
                                # and contains memory address of C entry point
        # Call the C function whose address is stored in %eax
        # - parentheses get the address that %eax is pointing to
        call        *(%eax)

	#=====================================================================
	# The rest of this file contains helper functions for the 16-bit
	# portion of the bootloader, so switch back to 16-bit code generation.
	.code16

halt:
	cli			# Disable interrupts so we can't be waken up
halt_loop:
	hlt			# Halt the CPU
	jmp	halt_loop	# Just in case we wake up, go back to sleep

boot_fail:
	mov	$STR_FAILED, %si
	call	print_str

	# Notify BIOS that boot failed.  Normally you would do this with
	# int $0x18, but we just want to stop.
	jmp	halt

# This helper function accepts a pointer to a string in %si, and outputs
# it to the screen using BIOS interrupt 10.
print_str:
	pusha
	sub	%bh, %bh		# Page to write to
	mov	$0x0e, %ah

	movb	(%si), %al		# Check first character - if it is 0,
	test	%al, %al		# skip over the entire loop.
	jz	write_str_done
write_str_loop:
	int	$0x10
	inc	%si			# Go on to the next character
	movb	(%si), %al		# If it's 0, exit loop.  Otherwise,
	test	%al, %al		# go back and output the character.
	jnz	write_str_loop
write_str_done:
	popa
	ret

	#=====================================================================
	# String literals used by our boot loader.

STR_LOADING:	.string "Loading program.\r\n"
STR_LOADED:	.string "Loaded.\r\n"
STR_FAILED:	.string "Failed to load program.\r\n"

	#=====================================================================
	# Global Descriptor Table for protected-mode operation
	# (See IA32 Manual, Volume 3A, Section 3.4.5 for description of bits)

	.align 8
gdt:
	.quad 0x0000000000000000	# Null segment.  Not used by CPU.
	.quad 0x00cf9a000000ffff	# Code segment, base 0, limit 4 GB.
	.quad 0x00cf92000000ffff        # Data segment, base 0, limit 4 GB.

gdtdesc:
	.word	gdtdesc - gdt - 1	# Size of the GDT, minus 1 byte.
	.long	gdt			# Address of the GDT.

	#=====================================================================
	# This is the signature at the end of the sector that tells the BIOS
	# that this is a boot sector.
	.org BOOT_SIG_ADDR - BOOT_BASE_ADDR
	.word 0xaa55

