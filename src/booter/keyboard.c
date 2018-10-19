#include "ports.h"
#include "keyboard.h"
#include "interrupts.h"
#include "handlers.h"

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

int static volatile a_pressed = 0;
int static volatile z_pressed = 0;
int static volatile k_pressed = 0;
int static volatile m_pressed = 0;

int is_a_pressed(void) {
    return a_pressed;
};
int is_z_pressed(void) {
    return z_pressed;
};
int is_k_pressed(void) {
    return k_pressed;
};
int is_m_pressed(void) {
    return m_pressed;
};


/* We maintain simple boolean variables that contain whether a certain
 * key is pressed. These variables are only modified through the keyboard
 * handler.
 */

void keyboard_handler(void) {
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
        case K_PRESS:
            k_pressed = 1;
        case K_RELEASE:
            k_pressed = 0;
        case M_PRESS:
            m_pressed = 1;
        case M_RELEASE:
            m_pressed = 0;
    }
}

void init_keyboard(void) {
     // Set to 0x21 because we remapped the interrupts to 0x20
     install_interrupt_handler(KEYBOARD_INTERRUPT, irq1_handler);
}
