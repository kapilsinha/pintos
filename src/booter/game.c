/* This is the entry-point for the game! */
#include "interrupts.h"
#include "keyboard.h"
void c_start(void) {
    /* TODO:  You will need to initialize various subsystems here.  This
     *        would include the interrupt handling mechanism, and the various
     *        systems that use interrupts.  Once this is done, you can call
     *        enable_interrupts() to start interrupt handling, and go on to
     *        do whatever else you decide to do!
     */
     init_interrupts();
     init_keyboard();
     enable_interrupts();

    /* Loop forever, so that we don't fall back into the bootloader code. */
    while (1) {
        if (is_a_pressed()) {
            break;
        }
    }
}
