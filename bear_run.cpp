/*  Bear Run – SDL2 side-scroller
    Build:  see Makefile  (or: g++ bear_run.cpp -o bear_run `sdl2-config --cflags --libs` -lSDL2_ttf -std=c++17)
    Keys:   SPACE / UP   – jump (double-jump supported)
            DOWN         – duck
*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <random>

// ─────────────────────────────────────────────────────────────────────────────
//  Constants
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int   WIN_W        = 900;
static constexpr int   WIN_H        = 340;
static constexpr int   FPS          = 60;
static constexpr int   FRAME_MS     = 1000 / FPS;

static constexpr float GND_Y        = 248.f;   // top of ground surface
static constexpr float GRAVITY      = 0.55f;
static constexpr float JUMP_VEL     = -13.5f;
static constexpr float DUCK_H_RATIO = 0.55f;   // height fraction when ducking

// Colour helpers
static SDL_Color Col(Uint8 r,Uint8 g,Uint8 b,Uint8 a=255){return {r,g,b,a};}

// ─────────────────────────────────────────────────────────────────────────────
//  Maths helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::mt19937 rng(std::random_device{}());
static float frand(float lo,float hi){
    return std::uniform_real_distribution<float>(lo,hi)(rng);
}
static int irand(int lo,int hi){
    return std::uniform_int_distribution<int>(lo,hi)(rng);
}
static bool rectsOverlap(float ax,float ay,float aw,float ah,
                          float bx,float by,float bw,float bh){
    return ax < bx+bw && ax+aw > bx && ay < by+bh && ay+ah > by;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing helpers
// ─────────────────────────────────────────────────────────────────────────────
static void setCol(SDL_Renderer* r, SDL_Color c){
    SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);
}
static void fillRect(SDL_Renderer* r,float x,float y,float w,float h,SDL_Color c){
    setCol(r,c);
    SDL_FRect rc{x,y,w,h};
    SDL_RenderFillRectF(r,&rc);
}
static void fillCircle(SDL_Renderer* r,float cx,float cy,float rad,SDL_Color c){
    setCol(r,c);
    int ir=(int)rad;
    for(int dy=-ir;dy<=ir;++dy)
        for(int dx=-ir;dx<=ir;++dx)
            if(dx*dx+dy*dy<=ir*ir)
                SDL_RenderDrawPoint(r,(int)(cx+dx),(int)(cy+dy));
}
static void fillEllipse(SDL_Renderer* r,float cx,float cy,float rx,float ry,SDL_Color c){
    setCol(r,c);
    int y0=(int)(cy-ry), y1=(int)(cy+ry);
    for(int py=y0;py<=y1;++py){
        float dy=(py-cy)/ry;
        float span=rx*std::sqrt(std::max(0.f,1.f-dy*dy));
        SDL_RenderDrawLine(r,(int)(cx-span),py,(int)(cx+span),py);
    }
}
static void drawLine(SDL_Renderer* r,float x1,float y1,float x2,float y2,
                     int thick, SDL_Color c){
    setCol(r,c);
    // thicken by drawing multiple shifted lines
    for(int t=-(thick/2);t<=(thick/2);++t)
        SDL_RenderDrawLine(r,(int)x1+t,(int)y1,(int)x2+t,(int)y2);
}
static void renderText(SDL_Renderer* rend, TTF_Font* font,
                        const std::string& txt, float x, float y,
                        SDL_Color c, bool centre=false){
    if(!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, txt.c_str(), c);
    if(!surf) return;
    SDL_Texture* tex  = SDL_CreateTextureFromSurface(rend, surf);
    if(!tex){ SDL_FreeSurface(surf); return; }
    int w=surf->w, h=surf->h;
    SDL_FreeSurface(surf);
    if(centre) x -= w/2.f;
    SDL_FRect dst{x,y,(float)w,(float)h};
    SDL_RenderCopyF(rend, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Particle
// ─────────────────────────────────────────────────────────────────────────────
struct Particle {
    float x,y,vx,vy,life,maxLife,r;
    SDL_Color col;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Obstacle types
// ─────────────────────────────────────────────────────────────────────────────
enum class ObsType { LOG, CACTUS, ROCK, BEE };

struct Obstacle {
    ObsType type;
    float   x, y;         // y = ground contact point (or flight height for bee)
    float   w, h;
    float   phase;        // used for bee bobbing
    bool    dead = false;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Coin
// ─────────────────────────────────────────────────────────────────────────────
struct Coin {
    float x,y,phase;
    bool collected=false;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Bear
// ─────────────────────────────────────────────────────────────────────────────
struct Bear {
    float x      = 90.f;
    float y      = GND_Y;        // bottom of bear feet
    float vy     = 0.f;
    float w      = 38.f;
    float h      = 48.f;
    int   jumps  = 0;            // 0=on ground; 1=first used; 2=double used
    bool  ducking= false;
    int   frame  = 0;            // walk frame counter
    int   blinkT = 80;           // countdown until blink

    // hitbox
    SDL_FRect hitbox() const {
        float bh = ducking ? h*DUCK_H_RATIO : h;
        return {x - w/2.f, y - bh, w*0.80f, bh*0.88f};
    }
    bool onGround() const { return jumps == 0; }

    void jump(){
        if(jumps < 2){
            vy = JUMP_VEL;
            ++jumps;
        }
    }

    void update(bool duckPressed){
        // gravity
        vy += GRAVITY;
        y  += vy;

        // ground collision
        if(y >= GND_Y){
            y      = GND_Y;
            vy     = 0.f;
            jumps  = 0;
        }
        ducking = duckPressed && onGround();

        // walk animation
        if(onGround()) ++frame;

        // blink logic
        --blinkT;
        if(blinkT < 0) blinkT = irand(60,160);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing: Bear
// ─────────────────────────────────────────────────────────────────────────────
static void drawBear(SDL_Renderer* r, const Bear& b, int globalFrame, bool flash){
    if(flash) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    float alpha = flash ? 130.f : 255.f;
    (void)alpha; // per-call alpha would need texture; we just skip drawing every other frame
    if(flash && (globalFrame/4)%2==0) return;

    float bx = b.x;
    float by = b.y;
    float bh = b.ducking ? b.h * DUCK_H_RATIO : b.h;

    auto BC  = Col(139, 94, 60);   // main brown
    auto BLT = Col(212,165,116);   // belly / snout light
    auto BDK = Col(90, 55, 25);    // limbs dark

    // ── legs ──
    float legSwing = b.onGround() ? std::sin(b.frame * 0.25f) * 10.f : 0.f;
    if(b.ducking){
        drawLine(r, bx-10,by-8, bx-20,by, 9, BDK);
        drawLine(r, bx+10,by-8, bx+20,by, 9, BDK);
    } else {
        drawLine(r, bx-8, by-14, bx-8+legSwing, by+2, 9, BDK);
        drawLine(r, bx+8, by-14, bx+8-legSwing, by+2, 9, BDK);
        // arms
        drawLine(r, bx-4,  by-bh*0.55f, bx-16, by-bh*0.35f+legSwing*0.5f, 7, BDK);
        drawLine(r, bx+14, by-bh*0.55f, bx+26, by-bh*0.35f-legSwing*0.5f, 7, BDK);
    }

    // ── body ──
    fillEllipse(r, bx, by-bh*0.45f, b.w*0.45f, bh*0.44f, BC);
    // belly
    fillEllipse(r, bx, by-bh*0.38f, b.w*0.28f, bh*0.30f, BLT);

    if(!b.ducking){
        // ── head ──
        fillCircle(r, bx+10, by-bh+12, 18.f, BC);
        // ears
        fillCircle(r, bx+2,  by-bh+2,  8.f, BC);
        fillCircle(r, bx+21, by-bh+2,  8.f, BC);
        fillCircle(r, bx+2,  by-bh+2,  4.5f,BLT);
        fillCircle(r, bx+21, by-bh+2,  4.5f,BLT);
        // snout
        fillEllipse(r, bx+18, by-bh+20, 10.f, 7.5f, BLT);
        // nose
        fillEllipse(r, bx+19, by-bh+15, 4.f, 3.f, Col(30,10,0));
        // eyes
        bool eyeOpen = (b.blinkT > 8);
        if(eyeOpen){
            fillCircle(r, bx+8,  by-bh+11, 3.5f, Col(20,20,20));
            fillCircle(r, bx+21, by-bh+11, 3.5f, Col(20,20,20));
            fillCircle(r, bx+9,  by-bh+10, 1.2f, Col(255,255,255));
            fillCircle(r, bx+22, by-bh+10, 1.2f, Col(255,255,255));
        } else {
            // blink
            drawLine(r, bx+5,by-bh+11, bx+12,by-bh+11, 2, Col(20,20,20));
            drawLine(r, bx+18,by-bh+11,bx+25,by-bh+11,2, Col(20,20,20));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing: Obstacles
// ─────────────────────────────────────────────────────────────────────────────
static void drawObstacle(SDL_Renderer* r, const Obstacle& o, int gf){
    float bx=o.x, by=o.y, bw=o.w, bh=o.h;
    switch(o.type){
    case ObsType::LOG:
        fillRect(r, bx,by-bh, bw,bh, Col(120,53,15));
        for(int i=1;i<4;++i)
            fillRect(r, bx+2, by-bh+i*13, bw-4, 4, Col(161,98,7));
        // bark texture lines
        for(int i=0;i<3;++i)
            drawLine(r, bx+4,by-bh+i*15+5, bx+bw-4,by-bh+i*15+5, 1, Col(90,40,5));
        break;

    case ObsType::CACTUS:{
        auto CG = Col(21,128,61);
        auto CL = Col(34,197,94);
        // trunk
        fillRect(r, bx+8,by-bh, 11,bh, CG);
        // arms
        fillRect(r, bx,   by-bh*0.60f, 9, 19, CG);
        fillRect(r, bx+16,by-bh*0.75f, 9, 24, CG);
        // tips
        fillRect(r, bx,   by-bh*0.60f-7, 9, 9, CG);
        fillRect(r, bx+16,by-bh*0.75f-7, 9, 9, CG);
        // highlights
        fillRect(r, bx+10,by-bh+2, 4,bh-4, CL);
        break;
    }
    case ObsType::ROCK:
        fillEllipse(r, bx+bw/2, by-bh/2, bw/2, bh/2, Col(100,116,139));
        fillEllipse(r, bx+bw/2-6, by-bh*0.70f, bw*0.22f, bh*0.18f, Col(148,163,184));
        break;

    case ObsType::BEE:{
        float bob = std::sin(gf*0.12f + o.phase)*6.f;
        float ex=bx, ey=by+bob;
        // wings
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        setCol(r, Col(186,230,253,160));
        // left wing
        for(int dy=-6;dy<=6;++dy){
            float span=10.f*std::sqrt(std::max(0.f,1.f-(dy/6.f)*(dy/6.f)));
            SDL_RenderDrawLine(r,(int)(ex+2-span),(int)(ey-22+dy),(int)(ex+2+span),(int)(ey-22+dy));
        }
        // right wing
        for(int dy=-6;dy<=6;++dy){
            float span=10.f*std::sqrt(std::max(0.f,1.f-(dy/6.f)*(dy/6.f)));
            SDL_RenderDrawLine(r,(int)(ex+18-span),(int)(ey-20+dy),(int)(ex+18+span),(int)(ey-20+dy));
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        // body
        fillEllipse(r, ex+15, ey-10, 15,10, Col(251,191,36));
        // stripes
        for(int s=0;s<3;++s) fillRect(r, ex+6+s*6,ey-18, 3,16, Col(20,20,20));
        // head
        fillCircle(r, ex+4, ey-11, 7, Col(251,191,36));
        // eye
        fillCircle(r, ex+2, ey-13, 2.5f, Col(20,20,20));
        fillCircle(r, ex+3, ey-14, 1.f,  Col(255,255,255));
        // stinger
        drawLine(r, ex+29,ey-10, ex+35,ey-8, 3, Col(146,64,14));
        break;
    }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing: Coin
// ─────────────────────────────────────────────────────────────────────────────
static void drawCoin(SDL_Renderer* r, const Coin& c, int gf){
    float bob = std::sin(gf*0.10f + c.phase)*3.f;
    float cx=c.x+8, cy=c.y-8+bob;
    // glow ring
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for(int ring=14;ring>8;--ring){
        Uint8 a = (Uint8)(40 - (14-ring)*5);
        setCol(r, {251,191,36,a});
        for(int dy=-ring;dy<=ring;++dy)
            for(int dx=-ring;dx<=ring;++dx)
                if(dx*dx+dy*dy<=ring*ring && dx*dx+dy*dy>=(ring-1)*(ring-1))
                    SDL_RenderDrawPoint(r,(int)(cx+dx),(int)(cy+dy));
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    // coin
    fillCircle(r, cx, cy, 9.f, Col(251,191,36));
    fillCircle(r, cx-2, cy-2, 5.f, Col(254,240,138));
    // "$" approximated by a rect cross
    fillRect(r, cx-1,cy-6, 2,12, Col(180,120,0));
    fillRect(r, cx-4,cy-1, 8,2,  Col(180,120,0));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing: Background
// ─────────────────────────────────────────────────────────────────────────────
struct Star { float x,y,r,phase; };
struct Cloud{ float x,y,w,speed; };

static void drawBackground(SDL_Renderer* r,
                            std::vector<Star>& stars,
                            std::vector<Cloud>& clouds,
                            int gf, float speed){
    // sky gradient (horizontal lines)
    for(int py=0;py<WIN_H;++py){
        float t=(float)py/WIN_H;
        Uint8 R=(Uint8)(26  + t*(59-26));
        Uint8 G=(Uint8)(10  + t*(31-10));
        Uint8 B=(Uint8)(46  + t*(106-46));
        SDL_SetRenderDrawColor(r,R,G,B,255);
        SDL_RenderDrawLine(r,0,py,WIN_W,py);
    }

    // moon
    fillCircle(r, 780, 45, 24, Col(254,249,195));
    fillCircle(r, 792, 40, 20, Col(26,10,46));  // crescent cut-out

    // stars
    for(auto& s:stars){
        s.phase+=0.03f;
        Uint8 a=(Uint8)(128+127*std::sin(s.phase));
        setCol(r, {224,215,255,a});
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_RenderDrawPoint(r,(int)s.x,(int)s.y);
        if(s.r>1.2f) SDL_RenderDrawPoint(r,(int)s.x+1,(int)s.y);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // far trees (parallax layer 1 – slowest)
    float treeOffset = std::fmod(gf * speed * 0.25f, 80.f);
    for(int ti=0;ti<14;++ti){
        float tx = ti*80.f - treeOffset;
        if(tx < -80) tx += 14*80.f;
        float th = 35.f+std::sin(ti*1.3f)*18.f;
        fillRect(r, tx+10, GND_Y-th-8, 9, 10, Col(10,4,28,200));
        // triangle crown via filled rows
        for(int row=0;row<(int)th;++row){
            float half=((float)row/th)*15.f;
            SDL_SetRenderDrawColor(r,10,4,28,200);
            SDL_RenderDrawLine(r,(int)(tx+14-half),(int)(GND_Y-th-8+row),
                                 (int)(tx+14+half),(int)(GND_Y-th-8+row));
        }
    }

    // mid trees (layer 2)
    float tree2Off = std::fmod(gf * speed * 0.55f, 100.f);
    for(int ti=0;ti<10;++ti){
        float tx = ti*100.f - tree2Off;
        if(tx < -100) tx += 10*100.f;
        float th = 50.f+std::sin(ti*2.1f)*20.f;
        fillRect(r, tx+10,GND_Y-th-10, 10,12, Col(15,6,40,220));
        for(int row=0;row<(int)th;++row){
            float half=((float)row/th)*18.f;
            SDL_SetRenderDrawColor(r,15,6,40,220);
            SDL_RenderDrawLine(r,(int)(tx+14-half),(int)(GND_Y-th-10+row),
                                 (int)(tx+14+half),(int)(GND_Y-th-10+row));
        }
    }

    // clouds
    for(auto& c:clouds){
        c.x -= c.speed * (speed/5.f);
        if(c.x+c.w < 0) c.x = WIN_W+20;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        setCol(r,{167,139,250,45});
        fillEllipse(r, c.x+c.w/2, c.y,     c.w/2,  18, {167,139,250,45});
        fillEllipse(r, c.x+c.w*0.3f,c.y+6, c.w*0.28f,14,{167,139,250,45});
        fillEllipse(r, c.x+c.w*0.7f,c.y+6, c.w*0.25f,14,{167,139,250,45});
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // ground dirt
    fillRect(r, 0, GND_Y+4, WIN_W, WIN_H-GND_Y, Col(146,64,14));
    // grass strip
    fillRect(r, 0, GND_Y-4, WIN_W, 14, Col(74,222,128));
    // grass tuft detail
    for(int gx=0;gx<WIN_W;gx+=16){
        float gh = 5.f+std::sin(gx*0.3f+gf*0.05f)*3.f;
        fillRect(r, (float)gx, GND_Y-8, 9, gh, Col(34,197,94));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HUD
// ─────────────────────────────────────────────────────────────────────────────
static void drawHUD(SDL_Renderer* r, TTF_Font* font, TTF_Font* bigFont,
                    int score, int coins, int lives, int best){
    // semi-transparent top bar
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    setCol(r,{10,3,30,140});
    SDL_FRect bar{0,0,(float)WIN_W,36};
    SDL_RenderFillRectF(r,&bar);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);

    renderText(r,font, "BEAR RUN",   8, 6,  Col(255,224,102));
    renderText(r,font, "Score: "+std::to_string(score),  180, 6, Col(255,255,255));
    renderText(r,font, "Coins: "+std::to_string(coins),  380, 6, Col(251,191,36));
    // hearts
    std::string hearts;
    for(int i=0;i<lives;++i) hearts += "<3 ";
    renderText(r,font, hearts,        580, 6, Col(248,113,113));
    renderText(r,font, "Best: "+std::to_string(best),    760, 6, Col(167,139,250));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Overlay screens
// ─────────────────────────────────────────────────────────────────────────────
static void drawIdle(SDL_Renderer* r, TTF_Font* big, TTF_Font* med){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    setCol(r,{10,3,30,180});
    SDL_FRect ov{0,0,(float)WIN_W,(float)WIN_H};
    SDL_RenderFillRectF(r,&ov);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);

    renderText(r,big,"BEAR RUN",    WIN_W/2+3,95, Col(124,58,237),true); // shadow
    renderText(r,big,"BEAR RUN",    WIN_W/2,  92, Col(255,224,102),true);
    renderText(r,med,"Press SPACE or UP to start", WIN_W/2,160, Col(224,215,255),true);
    renderText(r,med,"UP/SPACE=jump  DOWN=duck  Double-jump enabled!", WIN_W/2,200, Col(167,139,250),true);
    renderText(r,med,"Dodge obstacles - grab coins - survive!", WIN_W/2,228, Col(134,239,172),true);
}

static void drawDead(SDL_Renderer* r, TTF_Font* big, TTF_Font* med,
                     int score, int coins, int best){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    setCol(r,{10,3,30,200});
    SDL_FRect ov{0,0,(float)WIN_W,(float)WIN_H};
    SDL_RenderFillRectF(r,&ov);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);

    renderText(r,big,"GAME OVER",   WIN_W/2+3,98, Col(127,29,29),true);
    renderText(r,big,"GAME OVER",   WIN_W/2,  95, Col(252,165,165),true);
    renderText(r,med,"Score: "+std::to_string(score)+"   Coins: "+std::to_string(coins),
               WIN_W/2, 155, Col(254,240,138),true);
    if(score>0 && score>=best)
        renderText(r,med,"** NEW BEST! **", WIN_W/2,185, Col(251,191,36),true);
    renderText(r,med,"Press SPACE to play again", WIN_W/2,220, Col(196,181,253),true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game state reset
// ─────────────────────────────────────────────────────────────────────────────
struct GameState {
    enum class Mode { IDLE, PLAYING, DEAD } mode = Mode::IDLE;

    Bear  bear;
    std::vector<Obstacle>  obstacles;
    std::vector<Coin>      coins_v;
    std::vector<Particle>  particles;
    std::vector<Star>      stars;
    std::vector<Cloud>     clouds;

    int   score      = 0;
    int   coinCount  = 0;
    int   lives      = 3;
    int   best       = 0;
    int   frames     = 0;
    float speed      = 5.f;
    int   invTimer   = 0;   // invincibility frames after hit
    int   nextObs    = 80;
    int   nextCoin   = 120;

    void initBackground(){
        stars.clear(); clouds.clear();
        for(int i=0;i<90;++i)
            stars.push_back({frand(0,WIN_W),frand(0,130),frand(0.5f,2.f),frand(0,6.28f)});
        for(int i=0;i<6;++i)
            clouds.push_back({frand(0,WIN_W),frand(20,80),frand(60,130),frand(0.3f,0.7f)});
    }

    void reset(){
        bear     = Bear{};
        obstacles.clear();
        coins_v.clear();
        particles.clear();
        score    = 0;
        coinCount= 0;
        lives    = 3;
        frames   = 0;
        speed    = 5.f;
        invTimer = 0;
        nextObs  = 80;
        nextCoin = 120;
        mode     = Mode::PLAYING;
    }

    void addParticles(float x,float y,SDL_Color c,int n=8){
        for(int i=0;i<n;++i){
            float life = frand(0.5f,1.f);
            particles.push_back({x,y,
                frand(-3,3), frand(-4,0.5f),
                life, life,
                frand(3,8), c});
        }
    }

    void spawnObstacle(){
        ObsType t = (ObsType)irand(0,3);
        Obstacle o;
        o.type  = t;
        o.phase = frand(0,6.28f);
        o.x     = WIN_W + 20.f;
        switch(t){
        case ObsType::LOG:    o.w=28; o.h=42; o.y=GND_Y; break;
        case ObsType::CACTUS: o.w=26; o.h=58; o.y=GND_Y; break;
        case ObsType::ROCK:   o.w=44; o.h=30; o.y=GND_Y; break;
        case ObsType::BEE:    o.w=36; o.h=24; o.y=GND_Y-irand(60,110); break;
        }
        obstacles.push_back(o);
    }

    void spawnCoin(){
        float h = frand(0,1)>0.5f ? GND_Y-20 : GND_Y-80;
        coins_v.push_back({WIN_W+10.f, h, frand(0,6.28f)});
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/){

    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0){
        fprintf(stderr,"SDL_Init: %s\n",SDL_GetError());
        return 1;
    }
    if(TTF_Init() != 0){
        fprintf(stderr,"TTF_Init: %s\n",TTF_GetError());
    }

    SDL_Window* window = SDL_CreateWindow(
        "Bear Run",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, 0);
    if(!window){ fprintf(stderr,"Window: %s\n",SDL_GetError()); return 1; }

    SDL_Renderer* renderer = SDL_CreateRenderer(window,-1,
        SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer){ fprintf(stderr,"Renderer: %s\n",SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // ── fonts: try system paths, fall back to nullptr (still works, no text) ──
    TTF_Font* fontBig = nullptr;
    TTF_Font* fontMed = nullptr;
    TTF_Font* fontSm  = nullptr;
    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:\\Windows\\Fonts\\arial.ttf",
        nullptr
    };
    for(int i=0; fontPaths[i]; ++i){
        if(!fontBig) fontBig = TTF_OpenFont(fontPaths[i], 42);
        if(!fontMed) fontMed = TTF_OpenFont(fontPaths[i], 20);
        if(!fontSm)  fontSm  = TTF_OpenFont(fontPaths[i], 16);
        if(fontBig && fontMed && fontSm) break;
    }

    GameState gs;
    gs.initBackground();

    bool keyJump = false, keyDuck = false;
    bool quit = false;
    SDL_Event ev;
    Uint32 lastTick = SDL_GetTicks();

    while(!quit){
        // ── timing ──
        Uint32 now = SDL_GetTicks();
        Uint32 dt  = now - lastTick;
        if(dt < (Uint32)FRAME_MS){ SDL_Delay(FRAME_MS - dt); continue; }
        lastTick = now;

        // ── events ──
        bool jumpPressed = false;
        while(SDL_PollEvent(&ev)){
            if(ev.type == SDL_QUIT) quit=true;
            if(ev.type == SDL_KEYDOWN){
                auto sym = ev.key.keysym.sym;
                if(sym==SDLK_SPACE||sym==SDLK_UP||sym==SDLK_w){
                    keyJump=true; jumpPressed=true;
                }
                if(sym==SDLK_DOWN||sym==SDLK_s) keyDuck=true;
                if(sym==SDLK_ESCAPE) quit=true;
            }
            if(ev.type == SDL_KEYUP){
                auto sym = ev.key.keysym.sym;
                if(sym==SDLK_SPACE||sym==SDLK_UP||sym==SDLK_w) keyJump=false;
                if(sym==SDLK_DOWN||sym==SDLK_s) keyDuck=false;
            }
        }

        // ── state machine ──
        using Mode = GameState::Mode;

        if(gs.mode == Mode::IDLE){
            if(jumpPressed){ gs.reset(); }
        }
        else if(gs.mode == Mode::DEAD){
            if(jumpPressed){ gs.reset(); }
        }
        else { // PLAYING
            ++gs.frames;

            // speed ramp
            gs.speed = 5.f + gs.frames * 0.0018f;
            gs.score = gs.frames / 6;

            // jump input
            if(jumpPressed) gs.bear.jump();

            // update bear
            gs.bear.update(keyDuck);

            // invincibility countdown
            if(gs.invTimer > 0) --gs.invTimer;

            // spawn obstacles
            --gs.nextObs;
            if(gs.nextObs <= 0){
                gs.spawnObstacle();
                // sometimes spawn cluster
                if(frand(0,1)>0.75f) gs.spawnObstacle();
                // interval shrinks with speed, minimum 45 frames
                gs.nextObs = std::max(45,(int)(120 - gs.speed*6));
            }
            // spawn coins
            --gs.nextCoin;
            if(gs.nextCoin <= 0){
                gs.spawnCoin();
                gs.nextCoin = irand(55,100);
            }

            // move obstacles
            for(auto& o: gs.obstacles) o.x -= gs.speed;
            gs.obstacles.erase(std::remove_if(gs.obstacles.begin(),gs.obstacles.end(),
                [](const Obstacle& o){ return o.x + o.w < -10; }), gs.obstacles.end());

            // move coins
            for(auto& c: gs.coins_v) c.x -= gs.speed;
            gs.coins_v.erase(std::remove_if(gs.coins_v.begin(),gs.coins_v.end(),
                [](const Coin& c){ return c.x < -20 || c.collected; }), gs.coins_v.end());

            // bear hitbox
            SDL_FRect bh = gs.bear.hitbox();

            // coin collection
            for(auto& c: gs.coins_v){
                SDL_FRect ch{c.x, c.y-16, 18, 16};
                if(!c.collected &&
                   rectsOverlap(bh.x,bh.y,bh.w,bh.h, ch.x,ch.y,ch.w,ch.h)){
                    c.collected = true;
                    ++gs.coinCount;
                    gs.addParticles(c.x+9, c.y-8, Col(251,191,36), 6);
                }
            }

            // obstacle collision
            if(gs.invTimer == 0){
                for(auto& o: gs.obstacles){
                    float ox=o.x, oy=o.y, ow=o.w, oh=o.h;
                    // shrink hitbox a bit for fairness
                    ox+=4; ow-=8; oh-=4;
                    if(rectsOverlap(bh.x,bh.y,bh.w,bh.h, ox,oy-oh,ow,oh)){
                        --gs.lives;
                        gs.invTimer = 90;
                        gs.addParticles(gs.bear.x, gs.bear.y-gs.bear.h/2,
                                        Col(239,68,68), 12);
                        if(gs.lives <= 0){
                            gs.best = std::max(gs.best, gs.score);
                            gs.mode = Mode::DEAD;
                        }
                        break;
                    }
                }
            }

            // update particles
            for(auto& p: gs.particles){
                p.x += p.vx; p.y += p.vy; p.vy += 0.15f;
                p.life -= 0.025f;
            }
            gs.particles.erase(std::remove_if(gs.particles.begin(),gs.particles.end(),
                [](const Particle& p){ return p.life <= 0; }), gs.particles.end());
        }

        // ── render ──
        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderClear(renderer);

        drawBackground(renderer, gs.stars, gs.clouds, gs.frames, gs.speed);

        // coins
        for(auto& c: gs.coins_v)
            if(!c.collected) drawCoin(renderer, c, gs.frames);

        // obstacles
        for(auto& o: gs.obstacles) drawObstacle(renderer, o, gs.frames);

        // bear
        bool flash = (gs.invTimer > 0);
        drawBear(renderer, gs.bear, gs.frames, flash);

        // particles
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for(auto& p: gs.particles){
            Uint8 a=(Uint8)(p.life/p.maxLife*255);
            setCol(renderer,{p.col.r,p.col.g,p.col.b,a});
            SDL_FRect pr{p.x-p.r,p.y-p.r,p.r*2,p.r*2};
            SDL_RenderFillRectF(renderer,&pr);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        // HUD
        drawHUD(renderer, fontSm, fontBig, gs.score, gs.coinCount, gs.lives, gs.best);

        // overlays
        if(gs.mode == Mode::IDLE) drawIdle(renderer, fontBig, fontMed);
        if(gs.mode == Mode::DEAD) drawDead(renderer, fontBig, fontMed,
                                            gs.score, gs.coinCount, gs.best);

        SDL_RenderPresent(renderer);
    }

    if(fontBig) TTF_CloseFont(fontBig);
    if(fontMed) TTF_CloseFont(fontMed);
    if(fontSm)  TTF_CloseFont(fontSm);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}   
