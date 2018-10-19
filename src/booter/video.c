#include "video.h"
#include <stdint.h>

/* This is the address of the VGA text-mode video buffer.  Note that this
 * buffer actually holds 8 pages of text, but only the first page (page 0)
 * will be displayed.
 *
 * Individual characters in text-mode VGA are represented as two adjacent
 * bytes:
 *     Byte 0 = the character value
 *     Byte 1 = the color of the character:  the high nibble is the background
 *              color, and the low nibble is the foreground color
 *
 * See http://wiki.osdev.org/Printing_to_Screen for more details.
 *
 * Also, if you decide to use a graphical video mode, the active video buffer
 * may reside at another address, and the data will definitely be in another
 * format.  It's a complicated topic.  If you are really intent on learning
 * more about this topic, go to http://wiki.osdev.org/Main_Page and look at
 * the VGA links in the "Video" section.
 */
#define VIDEO_BUFFER ((void *) 0xB8000)

static char foreground_color;
static char background_color;
static uint16_t *video_buffer;
static Position cursor_pos;

void init_video(void) {
    foreground_color = LIGHT_GRAY;
    background_color = BLACK;
    video_buffer = VIDEO_BUFFER;
    cursor_pos.x = INIT_POS_CURSOR_X;
    cursor_pos.y = INIT_POS_CURSOR_Y;
}

void print_string(const char *str) {
    while (*str != '\0') {
        uint16_t info;
        
        if (cursor_pos.x <= WIDTH - 1) {
            info = (background_color << 12) | (foreground_color << 8) | *str;
            *video_buffer = info;
            video_buffer++;
            str++;
            cursor_pos.x++;
        }
        else if (cursor_pos.y <= HEIGHT - 1) {
            cursor_pos.x = 0;
            cursor_pos.y++;
        }
        else {
            clear_screen();
        }
    }
}

void clear_screen(void) {
    uint16_t *video_buffer_eraser = VIDEO_BUFFER;
    
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        *video_buffer_eraser = 0;
        video_buffer_eraser++;
    }
    
    video_buffer = VIDEO_BUFFER;
    cursor_pos.x = INIT_POS_CURSOR_X;
    cursor_pos.y = INIT_POS_CURSOR_Y;
}

char get_foreground_color(void) {
    return foreground_color;
}

void set_foreground_color(char color) {
    foreground_color = color;
}

char get_background_color(void) {
    return background_color;
}

void set_background_color(char color) {
    background_color = color;
}
