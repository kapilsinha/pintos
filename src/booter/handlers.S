# Note: interrupts are automatically disabled when the handler
# is invoked, so handlers don't need to worry about being interrupted
# by other handlers.  However, a handler can interrupt any other code!

.align 4
.globl irq0_handler
irq0_handler:
       pusha   # Save registers from the interrupted code!
       cld     # If calling C code, clear direction flag

       call timer_handler     # Do whatever you want to do here.

       # Acknowledge the interrupt so that the PIC will send more
       # interrupts!  We do this by sending 0x20 to port 0x20.
       # See http://wiki.osdev.org/8259_PIC#End_of_Interrupt for
       # details.
       mov     $0x20, %al
       out     %al, $0x20

       popa    # Restore all registers
       iret    # Go back to interrupted code


.align 4
.globl irq1_handler
irq1_handler:
       pusha   # Save registers from the interrupted code!
       cld     # If calling C code, clear direction flag

       call keyboard_handler     # Do whatever you want to do here.

       # Acknowledge the interrupt so that the PIC will send more
       # interrupts!  We do this by sending 0x20 to port 0x20.
       # See http://wiki.osdev.org/8259_PIC#End_of_Interrupt for
       # details.
       mov     $0x20, %al
       out     %al, $0x20

       popa    # Restore all registers
       iret    # Go back to interrupted code
