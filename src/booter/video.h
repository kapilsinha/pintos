#ifndef VIDEO_H
#define VIDEO_H


/* Available colors from the 16-color palette used for EGA and VGA, and
 * also for text-mode VGA output.
 */
#define BLACK          0
#define BLUE           1
#define GREEN          2
#define CYAN           3
#define RED            4
#define MAGENTA        5
#define BROWN          6
#define LIGHT_GRAY     7
#define DARK_GRAY      8
#define LIGHT_BLUE     9
#define LIGHT_GREEN   10
#define LIGHT_CYAN    11
#define LIGHT_RED     12
#define LIGHT_MAGENTA 13
#define YELLOW        14
#define WHITE         15

#define WIDTH  80
#define HEIGHT 25

#define INIT_POS_CURSOR_X 0
#define INIT_POS_CURSOR_Y 0


typedef struct position {
    char x;
    char y;
} Position;


void init_video(void);
void print_string(const char *str);
void clear_screen(void);
char get_foreground_color(void);
void set_foreground_color(char color);
char get_background_color(void);
void set_background_color(char color);

#endif /* VIDEO_H */
