#include <stdio.h>
#include <SDL2/SDL.h>

#define W        320
#define H        240
#define GND      185
#define GRAV     0.55f
#define JUMP_VEL -11.0f
#define BEAR_W   25
#define BEAR_H   34

// GAME ENTITY  

struct Bear {
    float x,y ;
    float velocityY;
    int onGround;

};

// PHYSICS 

void updateBear(struct Bear* bear) {
    // Apply gravity
    bear->velocityY += GRAV;
    bear->y += bear->velocityY;

    // Check ground collision
    if (bear->y > GND) {
        bear->y = GND;
        bear->velocityY = 0;
        bear->onGround = 1;
    }
}

// Simulate jump input
void jump(struct Bear* bear) {
    if (bear->onGround) {
         bear->velocityY = JUMP_VEL;
         bear->onGround = 0;
    }
}

int main() {
    struct Bear bear = {50, GND, 0, 1};

// ---Init SDL --
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    printf("SDL Init failed: %s\n", SDL_GetError());
    return 1;
}

SDL_Window* window = SDL_CreateWindow(
    "Bear Game",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    W, H,
    0
);

SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

int running = 1;
SDL_Event event;

// ---- game loop ---

while (running) {

    //INPUT 
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) running = 0;

        if (event.type == SDL_KEYDOWN &&
            event.key.keysym.sym == SDLK_SPACE) {
                jump(&bear);

            }
        }

    updateBear(&bear);

    // Clear screen (background)

    SDL_SetRenderDrawColor(renderer, 135, 206, 235, 255); // sky blue
    SDL_RenderClear(renderer);

    // Draw ground

SDL_SetRenderDrawColor(renderer, 0, 200, 0, 255);
SDL_Rect ground = {0, GND, W, H - GND};
SDL_RenderFillRect(renderer, &ground);

// Draw bear

SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
SDL_Rect bearRect = {
    (int)bear.x,
    (int)(bear.y - BEAR_H),
    BEAR_W,
    BEAR_H
};
SDL_RenderFillRect(renderer, &bearRect);

// Show everything on screen
SDL_RenderPresent(renderer);

SDL_Delay(16);

}

    return 0;
}



