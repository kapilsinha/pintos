#include "ports.h"

/* This is the IO port of the PS/2 controller, where the keyboard's scan
 * codes are made available.  Scan codes can be read as follows:
 *
 *     unsigned char scan_code = inb(KEYBOARD_PORT);
 *
 * Most keys generate a scan-code when they are pressed, and a second scan-
 * code when the same key is released.  For such keys, the only difference
 * between the "pressed" and "released" scan-codes is that the top bit is
 * cleared in the "pressed" scan-code, and it is set in the "released" scan-
 * code.
 *
 * A few keys generate two scan-codes when they are pressed, and then two
 * more scan-codes when they are released.  For example, the arrow keys (the
 * ones that aren't part of the numeric keypad) will usually generate two
 * scan-codes for press or release.  In these cases, the keyboard controller
 * fires two interrupts, so you don't have to do anything special - the
 * interrupt handler will receive each byte in a separate invocation of the
 * handler.
 *
 * See http://wiki.osdev.org/PS/2_Keyboard for details.
 */

// TODO: Could probably put all this into one function but nah
bool static volatile a_pressed = 0;
bool static volatile z_pressed = 0;
bool static volatile k_pressed = 0;
bool static volatile m_pressed = 0;

bool is_a_pressed(void) {
    return a_pressed;
};
bool is_z_pressed(void) {
    return z_pressed;
};
bool is_k_pressed(void) {
    return k_pressed;
};
bool is_m_pressed(void) {
    return m_pressed;
};


/* TODO:  You can create static variables here to hold keyboard state.
 *        Note that if you create some kind of circular queue (a very good
 *        idea, you should declare it "volatile" so that the compiler knows
 *        that it can be changed by exceptional control flow.
 *
 *        Also, don't forget that interrupts can interrupt *any* code,
 *        including code that fetches key data!  If you are manipulating a
 *        shared data structure that is also manipulated from an interrupt
 *        handler, you might want to disable interrupts while you access it,
 *        so that nothing gets mangled...
 */

void keyboard_interrupt(void) {
    /* Since we haven't acknowledged this interrupt yet, don't have to worry
        about other interrupts. */
    unsigned char scan_code = inb(KEYBOARD_PORT);
    switch(scan_code) {
        case A_PRESS:
            a_pressed = 1;
            break;
        case A_RELEASE:
            a_pressed = 0;
            break;
        case Z_PRESS:
            z_pressed = 1;
            break;
        case Z_RELEASE:
            z_pressed = 0;
            break;
    }
}


void init_keyboard(void) {
    /* TODO:  Initialize any state required by the keyboard handler. */

    /* TODO:  You might want to install your keyboard interrupt handler
     *        here as well.
     */
     install_interrupt_handler(1, irq1_handler)

}
