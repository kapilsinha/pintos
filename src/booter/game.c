#include <string.h>
#include "game.h"
#include "interrupts.h"
//#include "video.h"
#include "timer.h"
#include "keyboard.h"

static Game game;

/* This is the entry-point for the game! */
void c_start(void) {
    init_interrupts();
    init_video();
    init_timer();
    init_keyboard();
    
    enable_interrupts();
    clear_screen();
    print_string(welcome_screen);
    sleep(150);
    
    /* Loop forever, so that we don't fall back into the bootloader code. */
    while (1) {
        clear_screen();
        init_game();
	game.player_1.score = 0;
	game.player_2.score = 0;
    	game.is_game_over = 0;
    	game.is_round_over = 0;
        render_game_frame();
        while (! game.is_game_over) {
            sleep(5);
            clear_screen();
            run();
        }
	clear_screen();
        print_string("GAME OVER");
	sleep(400);
    }
}

void init_game(void) {
    game.player_1.paddle.top_left.x = 0;
    game.player_1.paddle.top_left.y = (int) (HEIGHT/2 - PADDLE_LENGTH/2);
    game.player_1.paddle.center.x = game.player_1.paddle.top_left.x
                                    + (int) (PADDLE_WIDTH/2);
    game.player_1.paddle.center.y = (int) (HEIGHT/2);
    game.player_1.paddle.bottom_right.x = game.player_1.paddle.top_left.x
                                          + (int) PADDLE_WIDTH;
    game.player_1.paddle.bottom_right.y = (int) (HEIGHT/2 + PADDLE_LENGTH/2);
    
    game.player_2.paddle.bottom_right.x = WIDTH - 1;
    game.player_2.paddle.bottom_right.y = (int) (HEIGHT/2 + PADDLE_LENGTH/2);
    game.player_2.paddle.center.x = game.player_2.paddle.bottom_right.x
                                    - (int) (PADDLE_WIDTH/2);
    game.player_2.paddle.center.y = (int) (HEIGHT/2);
    game.player_2.paddle.top_left.x = game.player_2.paddle.bottom_right.x
                                      - (int) (PADDLE_WIDTH);
    game.player_2.paddle.top_left.y = (int) (HEIGHT/2 - PADDLE_LENGTH/2);
    
    game.ball.position.x = (int) (WIDTH / 2);
    game.ball.position.y = (int) (HEIGHT / 2);
    game.ball.velocity.x = 1; // TODO: randomize this later
    game.ball.velocity.y = 0; // TODO: change to 1
}

void run(void) {
    compute_game_frame();
    if (game.is_round_over) {
        // Print score
        clear_screen();
	char score_player_1[20] = "Player 1 score: ";
	char score_player_2[30] = "   Player 2 score: ";
	if (game.player_1.score < 10) {
	    score_player_1[16] = '0' + game.player_1.score;
	}
	else { // Hard coding that max score is 2 digits
	    score_player_1[16] = '0' + (game.player_1.score / 10);
	    score_player_1[17] = '0' + (game.player_1.score - 10);
	}
	if (game.player_2.score < 10) {
	    score_player_2[19] = '0' + game.player_2.score;
	}
	else { // Hard coding that max score is 2 digits
	    score_player_2[19] = '0' + (game.player_2.score / 10);
	    score_player_2[20] = '0' + (game.player_2.score - 10);
	}
        print_string(score_player_1);
        print_string(score_player_2);
        sleep(150);
        init_game();
        render_game_frame();
	game.is_round_over = 0;
    }
    render_game_frame();
}

void compute_game_frame(void) {
    // Ball's direction is reversed and also if the location where the ball
    // hits the paddle affects the ball's velocity
    // (hitting the bottom of the paddle increments the ball's velocity and
    // hitting the top of the paddle decrements the ball's velocity)
    int third = PADDLE_LENGTH / 3;
    if (is_collision_paddle_1(&game.player_1.paddle, &game.ball)) {
    	game.ball.velocity.x *= -1;
    	game.ball.velocity.y *= -1;
        if (game.ball.position.y < game.player_1.paddle.top_left.y + third) {
            game.ball.velocity.y -= 1;
        }
	else if (game.ball.position.y
			> game.player_1.paddle.bottom_right.y - third) {
            game.ball.velocity.y += 1;
        }
    }
    if (is_collision_paddle_2(&game.player_2.paddle, &game.ball)) {
        game.ball.velocity.x *= -1;
        game.ball.velocity.y *= -1;
        if (game.ball.position.y < game.player_2.paddle.top_left.y + third) {
            game.ball.velocity.y -= 1;
        }
	else if (game.ball.position.y
			> game.player_2.paddle.bottom_right.y - third) {
            game.ball.velocity.y += 1;
        }
    }
    
    if (is_a_pressed()) {
        moveUp(&game.player_1.paddle);
    }
    if (is_z_pressed()) {
        moveDown(&game.player_1.paddle);
    }
    if (is_k_pressed()) {
        moveUp(&game.player_2.paddle);
    }
    if (is_m_pressed()) {
        moveDown(&game.player_2.paddle);
    }
    
    if (has_scored_player_1(&game.ball)) {
        game.player_1.score++;
	game.is_round_over = 1;
	clear_screen();
        if (is_winner(&game.player_1)) {
            game.is_game_over = 1;
            clear_screen();
            print_string("Player 1 wins!");
        }
    }
    if (has_scored_player_2(&game.ball)) {
        game.player_2.score++;
	game.is_round_over = 1;
        if (is_winner(&game.player_2)) {
            game.is_game_over = 1;
            clear_screen();
            print_string("Player 2 wins!");
        }
    }
    
    moveBall(&game.ball);
}

void render_game_frame(void) {
    char frame[HEIGHT * WIDTH];
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            if (j == WIDTH / 2) {
                frame[i * WIDTH + j] = '|';
	    }
	    else {
                frame[i * WIDTH + j] = ' ';
            }
	}
    }
    // memset(&frame, 0, (HEIGHT * WIDTH) * sizeof(char));

    for (int x = game.player_1.paddle.top_left.x;
             x <= game.player_1.paddle.bottom_right.x; x++) {
        for (int y = game.player_1.paddle.top_left.y;
                y <= game.player_1.paddle.bottom_right.y; y++) {
            frame[y * WIDTH + x] = (char) 219;
        }
    }
    
    for (int x = game.player_2.paddle.top_left.x;
             x <= game.player_2.paddle.bottom_right.x; x++) {
        for (int y = game.player_2.paddle.top_left.y;
                y <= game.player_2.paddle.bottom_right.y; y++) {
            frame[y * WIDTH + x] = (char) 219;
        }
    }
    
    frame[game.ball.position.y * WIDTH + game.ball.position.x] = (char) 219;
    print_string(frame);
}

int is_collision_paddle_1(Paddle *paddle, Ball *ball) {
    return (ball->position.x == paddle->bottom_right.x + 1
         && ball->position.y >= paddle->top_left.y
         && ball->position.y <= paddle->bottom_right.y);
}

int is_collision_paddle_2(Paddle *paddle, Ball *ball) {
    return (ball->position.x == paddle->top_left.x - 1
         && ball->position.y >= paddle->top_left.y
         && ball->position.y <= paddle->bottom_right.y);
}

int has_scored_player_1(Ball *ball) {
    return (ball->position.x == WIDTH - 1);
}

int has_scored_player_2(Ball *ball) {
    return (ball->position.x == 0);
}

void moveUp(Paddle *paddle) {
    if (paddle->top_left.y > 0) {
        paddle->top_left.y -= 1;
        paddle->center.y -= 1;
        paddle->bottom_right.y -= 1;
    }
}

void moveDown(Paddle *paddle) {
    if (paddle->bottom_right.y < HEIGHT - 1) {
        paddle->top_left.y += 1;
        paddle->center.y += 1;
        paddle->bottom_right.y += 1;
    }
}

void moveBall(Ball *ball) {
    if (ball->position.y <= 1 || ball->position.y >= HEIGHT - 2) {
        ball->velocity.y *= -1;
    }
    ball->position.x += ball->velocity.x;
    ball->position.y += ball->velocity.y;
}

int is_winner(Player *player) {
    return (player->score == MAX_SCORE);
}
