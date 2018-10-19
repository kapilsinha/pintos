#include <unistd.h>

#include "game.h"
#include "video.h"
#include "keyboard.h"
#include "interrupts.h"

static Game game;

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
    init_video();
    init_keyboard();
    
    enable_interrupts();
    clear_screen();
    print_string(welcome_screen);
    
    while (!is_a_pressed()) {
        
    }
    
    clear_screen();
    sleep(NUM_SECONDS_INIT_PAUSE);
    init_game();
    
    /* Loop forever, so that we don't fall back into the bootloader code. */
    while (1) {
        while (!is_done(game)) {
            run();
        }
    }
}

void init_game(void) {
    game.player_1.score = 0;
    game.player_1.paddle.length = PADDLE_LENGTH;
    game.player_1.paddle.position.x = INIT_POS_P1_X;
    game.player_1.paddle.position.y = INIT_POS_P1_Y;
    game.player_1.paddle.velocity.x = INIT_VEL_P1_X;
    game.player_1.paddle.velocity.y = INIT_VEL_P1_Y;
    
    game.player_2.score = 0;
    game.player_2.paddle.length = PADDLE_LENGTH;
    game.player_2.paddle.position.x = INIT_POS_P2_X;
    game.player_2.paddle.position.y = INIT_POS_P2_Y;
    game.player_2.paddle.velocity.x = INIT_VEL_P2_X;
    game.player_2.paddle.velocity.y = INIT_VEL_P2_Y;
    
    game.ball.position.x = WIDTH / 2;
    game.ball.position.y = HEIGHT / 2;
    game.ball.velocity.x = 1;
    game.ball.velocity.y = 1;
}

void run(void);
    compute_game_frame();
    render_game_frame();
}


void compute_game_frame(void) {
    if (is_winner(game.player_1)) {
        clear_screen();
        print_string("Player 1 wins!");
    }
    if (is_winner(game.player_2)) {
        clear_screen();
        print_string("Player 2 wins!");
    }
 

    if (is_collision(game.player_1.paddle, game.ball)) {
        game.ball.velocity.x = -game.ball.velocity.x;
        game.ball.velocity.y += game.player_1.paddle.velocity.y;
    }
    if (is_collision(game.player_2.paddle, game.ball)) {
        game.ball.velocity.x = -game.ball.velocity.x;
        game.ball.velocity.y += game.player_2.paddle.velocity.y;
    }
    
    if (is_a_pressed()) {
        game.player_1.paddle.velocity.x--;
    }
    if (is_z_pressed()) {
        game.player_1.paddle.velocity.x++;
    }
    if (is_k_pressed()) {
        game.player_2.paddle.velocity.x--;
    }
    if (is_m_pressed()) {
        game.player_2.paddle.velocity.x++;
    }
    
    if (has_scored_player_1(game.ball)) {
        game.player_1.score++;
    }
    if (has_scored_player_2(game.ball)) {
        game.player_2.score++;
    }
    
    game.player_1.paddle.position.x += game.player_1.paddle.velocity.x;
    game.player_1.paddle.position.y += game.player_1.paddle.velocity.y;
    game.player_2.paddle.position.x += game.player_2.paddle.velocity.x;
    game.player_2.paddle.position.y += game.player_2.paddle.velocity.y;
    
    game.ball.position.x += game.ball.velocity.x;
    game.ball.position.y += game.ball.velocity.y;
}

void render_game_frame(void) {
    char frame[HEIGHT * WIDTH];
    
    memset(&frame, 0, HEIGHT * WIDTH * sizeof(char));

    for (int p = 0; p < game.player_1.paddle.length; p++) {
        frame[p * WIDTH + game.player_1.paddle.position.x] = '|';
    }
    
    for (int p = 0; p < game.player_2.paddle.length; p++) {
        frame[p * WIDTH + game.player_2.paddle.position.x] = '|';
    }
    
    frame[game.ball.position.y * WIDTH + game.ball.position.x] = 'X';
    
    write_string(frame);
}

int is_collision(Paddle paddle, Ball ball) {
    for (int x = paddle.position.x - 1;
         x <= paddle.position.x + paddle.length + 1; x++) {
        for (int y = paddle.position.y - 1; y <= paddle.position.y + 1; y++) {
            if ((x == ball.position.x && (x >= 0 && x < WIDTH))
                && (y == ball.position.y && (y >= 0 && y < HEIGHT))) {
                return 1;
            }
        }
    }
    
    return 0;
}

int is_winner(Player player) {
    return player.score = MAX_SCORE;
}
