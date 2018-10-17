#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KEYBOARD_PORT 0x60
#define A_PRESS 0x1E
#define A_RELEASE 0x9E
#define Z_PRESS 0x2C
#define Z_RELEASE 0xAC
#define K_PRESS 0x25
#define K_RELEASE 0xA5
#define M_PRESS 0x32
#define M_RELEASE 0xB2
#define Q_PRESS 0x10

bool is_a_pressed(void);
bool is_z_pressed(void);
bool is_k_pressed(void);
bool is_m_pressed(void);
void keyboard_interrupt(void);
void init_keyboard(void);

#endif /* KEYBOARD_H */
