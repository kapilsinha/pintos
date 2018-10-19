#include "video.h"

#define NUM_SECONDS_INIT_PAUSE 100
#define INTERVAL 18

#define MAX_SCORE 11

#define INIT_POS_P1_X 0
#define INIT_POS_P1_Y 0
#define INIT_VEL_P1_X 0
#define INIT_VEL_P1_Y 0

#define INIT_POS_P2_X 0
#define INIT_POS_P2_Y 79
#define INIT_VEL_P2_X 0
#define INIT_VEL_P2_Y 0

#define PADDLE_LENGTH 3

#define TIMER_INTERRUPT 0x20
#define KEYBOARD_INTERRUPT 0x21

const char *welcome_screen = "PONG: Press A to start";

typedef struct velocity {
    int x;
    int y;
} Velocity;

typedef struct paddle {
    int length;
    Position position;
    Velocity velocity;
} Paddle;

typedef struct player {
    int score;
    Paddle paddle;
} Player;

typedef struct ball {
    Position position;
    Velocity velocity;
} Ball;

typedef struct game {
    Player player_1;
    Player player_2;
    Ball ball;
    int is_done;
} Game;

void init_game(void);
void render_frame(void);
