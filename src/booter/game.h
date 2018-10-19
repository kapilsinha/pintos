#include "video.h"

#define NUM_SECONDS_INIT_PAUSE 100
#define INTERVAL 18

#define MAX_SCORE 3

/*
#define INIT_POS_P1_X 0
#define INIT_POS_P1_Y 0
#define INIT_VEL_P1_X 0
#define INIT_VEL_P1_Y 0

#define INIT_POS_P2_X 0
#define INIT_POS_P2_Y 79
#define INIT_VEL_P2_X 0
#define INIT_VEL_P2_Y 0
*/

#define PADDLE_LENGTH 3
#define PADDLE_WIDTH 1

#define TIMER_INTERRUPT 0x20
#define KEYBOARD_INTERRUPT 0x21

const char *welcome_screen = "Let's play PONG!";

typedef struct velocity {
    int x;
    int y;
} Velocity;

typedef struct paddle {
    Position top_left;
    Position center;
    Position bottom_right;
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
    int is_round_over;
    int is_game_over;
} Game;

void init_game(void);
void run(void);
void compute_game_frame(void);
void render_game_frame(void);
int is_collision_paddle_1(Paddle *paddle, Ball *ball);
int is_collision_paddle_2(Paddle *paddle, Ball *ball);
int has_scored_player_1(Ball *ball);
int has_scored_player_2(Ball *ball);
void moveUp(Paddle *paddle);
void moveDown(Paddle *paddle);
void moveBall(Ball *ball);
int is_winner(Player *player);
