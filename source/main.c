/*
main.c is the entrypoint of your game. Everything starts from here.
This has the main loop for the game, which is then used to call out to other code.
*/

#include "source/neslib_asm/neslib.h"
#include "source/library/bank_helpers.h"
#include "source/configuration/game_states.h"
#include "source/menus/title.h"
#include "source/globals.h"
#include "source/menus/error.h"
#include "source/menus/credits.h"
#include "source/map/load_map.h"
#include "source/map/map.h"
#include "source/graphics/game_text.h"
#include "source/graphics/hud.h"
#include "source/graphics/fade_animation.h"
#include "source/sprites/player.h"
#include "source/menus/pause.h"
#include "source/sprites/map_sprites.h"
#include "source/sprites/sprite_definitions.h"
#include "source/menus/input_helpers.h"
#include "source/menus/game_over.h"

void draw_warning(void);


// Method to set a bunch of variables to default values when the system starts up.
// Note that if variables aren't set in this method, they will start at 0 on NES startup.
void initialize_variables(void) {

    playerOverworldPosition = 0; // Which tile on the overworld to start with; 0-62
    playerHealth = 1; // Player's starting health - how many hearts to show on the HUD.
    playerMaxHealth = 1; // Player's max health - how many hearts to let the player collect before it doesn't count.
    playerXPosition = (128 << PLAYER_POSITION_SHIFT); // X position on the screen to start (increasing numbers as you go left to right. Just change the number)
    playerYPosition = (128 << PLAYER_POSITION_SHIFT); // Y position on the screen to start (increasing numbers as you go top to bottom. Just change the number)
    playerDirection = SPRITE_DIRECTION_DOWN; // What direction to have the player face to start.
    playerKeyCount = 0;

    lastPlayerSpriteCollisionId = NO_SPRITE_HIT;

    currentWorldId = WORLD_OVERWORLD; // The ID of the world to load.
    currentLayer = 0;

    lastCheckpointScreenId = playerOverworldPosition;
    lastCheckpointLayer = currentLayer;
    lastCheckpointKeyCount = playerKeyCount;
    lastCheckpointPlayerX = playerXPosition;
    lastCheckpointPlayerY = playerYPosition;
    nearestCrack = 0xff;

    for (i = 0; i < 64; ++i) {
        lastCheckpointWorldState[i] = 0;
    }

    
    // Little bit of generic initialization below this point - we need to set
    // The system up to use a different hardware bank for sprites vs backgrounds.
    bank_spr(1);
}   

void main(void) {
    fade_out_instant();
    gameState = GAME_STATE_SYSTEM_INIT;

    while (1) {
        everyOtherCycle = !everyOtherCycle;
        switch (gameState) {
            case GAME_STATE_SYSTEM_INIT:
                initialize_variables();
                gameState = GAME_STATE_TITLE_DRAW;
                break;

            case GAME_STATE_TITLE_DRAW:
                banked_call(PRG_BANK_TITLE, draw_title_screen);
                music_play(SONG_TITLE);
                fade_in();
                break;
            case GAME_STATE_TITLE_INPUT:
                banked_call(PRG_BANK_TITLE, handle_title_input);
                break;
            case GAME_STATE_POST_TITLE:

                banked_call(PRG_BANK_TITLE, draw_intro);
                banked_call(PRG_BANK_TITLE, draw_warning);
                playerDeathCount = 0;

                music_stop();
                fade_out();
                load_map();

                banked_call(PRG_BANK_MAP_LOGIC, draw_current_map_to_a);
                
                // The draw map methods handle turning the ppu on/off, but we weren't quite done yet. Turn it back off.
                ppu_off();
                banked_call(PRG_BANK_MAP_LOGIC, init_map);
                banked_call(PRG_BANK_MAP_LOGIC, load_sprites);

                banked_call(PRG_BANK_HUD, draw_hud);
                ppu_on_all();

                // Seed the random number generator here, using the time since console power on as a seed
                set_rand(frameCount);
                
                // Map drawing is complete; let the player play the game!
                music_play(SONG_LAYERS + currentLayer);
                fade_in();
                gameState = GAME_STATE_RUNNING;
                break;

            case GAME_STATE_RUNNING:
                // TODO: Might be nice to have this only called when we have something to update, and maybe only update the piece we 
                // care about. (For example, if you get a key, update the key count; not everything!
                banked_call(PRG_BANK_HUD, update_hud);
                banked_call(PRG_BANK_MAP_SPRITES, update_map_sprites);
                banked_call(PRG_BANK_PLAYER_SPRITE, handle_player_movement);
                banked_call(PRG_BANK_PLAYER_SPRITE, update_player_sprite);
                break;
            case GAME_STATE_SCREEN_SCROLL:
                // Hide all non-player sprites in play, so we have an empty screen to add new ones to
                oam_hide_rest(FIRST_ENEMY_SPRITE_OAM_INDEX);
                crackTimer = 0; // Cancel crack creation, that does all kinds of bad stuff if it happens unexpectedly
                *((unsigned char*)0x204) = SPRITE_OFFSCREEN;

                // If you don't like the screen scrolling transition, you can replace the transition with `do_fade_screen_transition`
                banked_call(PRG_BANK_MAP_LOGIC, do_scroll_screen_transition);
                nearestCrack = 0xff;
                break;
            case GAME_STATE_SHOWING_TEXT:
                banked_call(PRG_BANK_GAME_TEXT, draw_game_text);
                gameState = GAME_STATE_RUNNING;
                break;
            case GAME_STATE_PAUSED:
                fade_out();
                banked_call(PRG_BANK_PAUSE_MENU, draw_pause_screen);
                fade_in();
                banked_call(PRG_BANK_PAUSE_MENU, handle_pause_input);

                // When we get here, the player has unpaused. 
                // Pause has its own mini main loop in handle_input to make logic easier.
                fade_out();

                if (gameState != GAME_STATE_GAME_OVER_1) {
                    banked_call(PRG_BANK_MAP_LOGIC, draw_current_map_to_a);
                    
                    // The draw map methods handle turning the ppu on/off, but we weren't quite done yet. Turn it back off.
                    ppu_off();
                    banked_call(PRG_BANK_MAP_LOGIC, init_map);
                    banked_call(PRG_BANK_HUD, draw_hud);
                    ppu_on_all();
                    fade_in();
                }

                break;
            case GAME_STATE_GAME_OVER:

                fade_out();
            case GAME_STATE_GAME_OVER_1:

                // Draw the "you lose" screen
                banked_call(PRG_BANK_GAME_OVER, draw_game_over_screen);
                sfx_play(SFX_GAMEOVER, SFX_CHANNEL_1);

                fade_in();
                banked_call(PRG_BANK_MENU_INPUT_HELPERS, wait_for_start);
                fade_out();
                

                banked_call(PRG_BANK_MAP_LOGIC, restore_game_over);
                load_map();
                banked_call(PRG_BANK_MAP_LOGIC, draw_current_map_to_a);

                ppu_off();
                banked_call(PRG_BANK_MAP_LOGIC, init_map);
                banked_call(PRG_BANK_MAP_LOGIC, load_sprites);

                banked_call(PRG_BANK_HUD, draw_hud);
                ppu_on_all();
                fade_in();
                music_play(SONG_LAYERS + currentLayer);
                gameState = GAME_STATE_RUNNING;

                break;
            case GAME_STATE_CREDITS:
                music_stop();
                sfx_play(SFX_WIN, SFX_CHANNEL_1);

                fade_out();
                // Draw the "you won" screen
                banked_call(PRG_BANK_CREDITS_MENU, draw_win_screen);
                fade_in();
                banked_call(PRG_BANK_MENU_INPUT_HELPERS, wait_for_start);
                fade_out();

                // Folow it up with the credits.
                banked_call(PRG_BANK_CREDITS_MENU, draw_credits_screen);
                fade_in();
                banked_call(PRG_BANK_MENU_INPUT_HELPERS, wait_for_start);
                fade_out();
                reset();
                break;
            default:
                crash_error_use_banked_details(ERR_UNKNOWN_GAME_STATE, ERR_UNKNOWN_GAME_STATE_EXPLANATION, "gameState value", gameState);
                
        }
        ppu_wait_nmi();
        
    }
}



// HACK: I kinda just threw this somewhere...
// NOTE: DO NOT USE ANY BUILD-IN VARS HERE FOR SCRATCH STORAGE. THIS RUNS DURING NMI
ZEROPAGE_DEF(unsigned char, derp);
void __fastcall__ do_ppu_mess(void) {
    if (gameState != GAME_STATE_RUNNING) {
        return;
    }
    if (currentLayer == 3) {
        if ((frameCount & 0xf0) < 0x40 && rand8() > 200) {
            derp = rand8();
            //*(unsigned char*)0x2000 = 0;
            __asm__("lda $2002");
            *(unsigned char*)0x2005 = derp;
            *(unsigned char*)0x2005 = rand8();

            // scroll(rand8(), rand8());
            for (derp = 0; derp < 200; ++derp) {
                __asm__("nop");
            }
        }

        if ((frameCount & 0xf0) == 0x20 && rand8() > 200) {
            set_chr_bank_0(CHR_BANK_TILES + currentLayer + 2);
        } else {
            set_chr_bank_0(CHR_BANK_TILES + currentLayer);
        }
    }

    if (currentLayer == 4) {
        if ((frameCount & 0xf0) > 0xb0 && rand8() > 100) {
            derp = rand8();
            //*(unsigned char*)0x2000 = 0;
            __asm__("lda $2002");
            *(unsigned char*)0x2005 = derp;
            *(unsigned char*)0x2005 = rand8();

            // scroll(rand8(), rand8());
            for (derp = 0; derp < 200; ++derp) {
                __asm__("nop");
            }
        }

        if ((frameCount & 0xf0) > 0x90 && rand8() > 100) {
            set_chr_bank_0(CHR_BANK_TILES + currentLayer + 2);
        } else {
            set_chr_bank_0(CHR_BANK_TILES + currentLayer);
        }

    }
}