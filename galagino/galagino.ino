/*
 * galagino.ino - Galaga arcade for ESP32 and Arduino IDE
 *
 * (c) 2023 Till Harbaum <till@harbaum.org>
 * 
 * Published under GPLv3
 *
 */

#include "config.h"

#include "driver/i2s.h"
#include "video.h"
#include "leds.h"

#include "emulation.h"

#include "tileaddr.h"

// include converted rom data
#ifdef ENABLE_PACMAN
#ifndef SINGLE_MACHINE
#include "pacman_logo.h"
#endif
#include "pacman_tilemap.h"
#include "pacman_spritemap.h"
#include "pacman_cmap.h"
#include "pacman_wavetable.h"
#endif

#ifdef ENABLE_GALAGA
#ifndef SINGLE_MACHINE
#include "galaga_logo.h"
#endif
#include "galaga_spritemap.h"
#include "galaga_tilemap.h"
#include "galaga_cmap_tiles.h"
#include "galaga_cmap_sprites.h"
#include "galaga_wavetable.h"
#include "galaga_sample_boom.h"
#include "galaga_starseed.h"
#endif

#ifdef ENABLE_DKONG
#ifndef SINGLE_MACHINE
#include "dkong_logo.h"
#endif
#include "dkong_tilemap.h"
#include "dkong_spritemap.h"
#include "dkong_cmap.h"

#include "dkong_sample_walk0.h"
#include "dkong_sample_walk1.h"
#include "dkong_sample_walk2.h"
#include "dkong_sample_jump.h"
#include "dkong_sample_stomp.h"
#endif

#ifdef ENABLE_FROGGER
#ifndef SINGLE_MACHINE
#include "frogger_logo.h"
#endif
#include "frogger_tilemap.h"
#include "frogger_spritemap.h"
#include "frogger_cmap.h"
#endif

#ifndef SINGLE_MACHINE
signed char machine = MCH_MENU;   // start with menu
#endif

// instance of main tft driver
Video tft = Video();

// buffer space for one row of 28 characters
unsigned short *frame_buffer;

TaskHandle_t emulationtask;

// the hardware supports 64 sprites
unsigned char active_sprites = 0;
struct sprite_S *sprite;

#ifdef ENABLE_GALAGA
unsigned char stars_scroll_y = 0;

// the ship explosion sound is stored as a digi sample.
// All other sounds are generated on the fly via the
// original wave tables
unsigned short snd_boom_cnt = 0;
const signed char *snd_boom_ptr = NULL;
#endif

#ifdef ENABLE_DKONG
unsigned short dkong_sample_cnt[3] = { 0,0,0 };
const signed char *dkong_sample_ptr[3];
#endif

#ifdef ENABLE_FROGGER
int ay_period[3] = {0,0,0};
int ay_volume[3] = {0,0,0};
int ay_enable[3] = {0,0,0};
int audio_cnt[3], audio_toggle[3] = {1,1,1};
extern unsigned char soundregs[];
#endif

// one method to return to the main menu is to reset
// the entire machine. The main disadvantage is a 
// short noise from the speaker during reset
extern "C" void hw_reset(void);
void hw_reset(void) {
  ESP.restart();
}

#ifdef ENABLE_PACMAN
void pacman_prepare_frame(void) {
  // Do all the preparations to render a screen.

  /* preprocess sprites */
  active_sprites = 0;
  for(int idx=0;idx<8 && active_sprites<92;idx++) {
    unsigned char *sprite_base_ptr = memory + 2*(7-idx);
    struct sprite_S spr;     
      
    spr.code = sprite_base_ptr[0x0ff0] >> 2;
    spr.color = sprite_base_ptr[0x0ff1] & 63;
    spr.flags = sprite_base_ptr[0x0ff0] & 3;
    
    // adjust sprite position on screen for upright screen
    spr.x = 255 - 16 - sprite_base_ptr[0x1060];
    spr.y = 16 + 256 - sprite_base_ptr[0x1061];

    if((spr.code < 64) &&
       (spr.y > -16) && (spr.y < 288) &&
       (spr.x > -16) && (spr.x < 224)) {      
      
      // save sprite in list of active sprites
      sprite[active_sprites++] = spr;
    }
  }
}
#endif

#ifdef ENABLE_GALAGA
void galaga_prepare_frame(void) {
  // Do all the preparations to render a screen.
  
  leds_state_reset();
  
  /* preprocess sprites */
  active_sprites = 0;
  for(int idx=0;idx<64 && active_sprites<92;idx++) {
    unsigned char *sprite_base_ptr = memory + 2*(63-idx);
    // check if sprite is visible
    if ((sprite_base_ptr[0x1b80 + 1] & 2) == 0) {
      struct sprite_S spr;     
      
      spr.code = sprite_base_ptr[0x0b80];
      spr.color = sprite_base_ptr[0x0b80 + 1];
      spr.flags = sprite_base_ptr[0x1b80];
      spr.x = sprite_base_ptr[0x1380] - 16;
      spr.y = sprite_base_ptr[0x1380 + 1] +
	      0x100*(sprite_base_ptr[0x1b80 + 1] & 1) - 40;

      if((spr.code < 128) &&
    	   (spr.y > -16) && (spr.y < 288) &&
	       (spr.x > -16) && (spr.x < 224)) {      

#ifdef LED_PIN
        leds_check_galaga_sprite(&spr);
#endif

    	  // save sprite in list of active sprites
	      sprite[active_sprites] = spr;
      	// for horizontally doubled sprites, this one becomes the code + 2 part
	      if(spr.flags & 0x08) sprite[active_sprites].code += 2;	
	      active_sprites++;
      }

      // handle horizontally doubled sprites
      if((spr.flags & 0x08) &&
    	   (spr.y > -16) && (spr.y < 288) &&
    	   ((spr.x+16) >= -16) && ((spr.x+16) < 224)) {
	      // place a copy right to the current one
	      sprite[active_sprites] = spr;
	      sprite[active_sprites].x += 16;
	      active_sprites++;
      }

      // handle vertically doubled sprites
      // (these don't seem to happen in galaga)
      if((spr.flags & 0x04) &&
	       ((spr.y+16) > -16) && ((spr.y+16) < 288) && 
	      (spr.x > -16) && (spr.x < 224)) {      
	      // place a copy below the current one
	      sprite[active_sprites] = spr;
	      sprite[active_sprites].code += 3;
	      sprite[active_sprites].y += 16;
	      active_sprites++;
      }
	
      // handle in both directions doubled sprites
      if(((spr.flags & 0x0c) == 0x0c) &&
	       ((spr.y+16) > -16) && ((spr.y+16) < 288) &&
	       ((spr.x+16) > -16) && ((spr.x+16) < 224)) {
	      // place a copy right and below the current one
	      sprite[active_sprites] = spr;
	      sprite[active_sprites].code += 1;
	      sprite[active_sprites].x += 16;
	      sprite[active_sprites].y += 16;
	      active_sprites++;
      }
    }
  }
}
#endif

#ifdef ENABLE_DKONG
void dkong_prepare_frame(void) {
  active_sprites = 0;
  for(int idx=0;idx<96 && active_sprites<92;idx++) {
    // sprites are stored at 0x7000
    unsigned char *sprite_base_ptr = memory + 0x1000 + 4*idx;
    struct sprite_S spr;     
    
    // adjust sprite position on screen for upright screen
    spr.x = sprite_base_ptr[0] - 23;
    spr.y = sprite_base_ptr[3] + 8;
    
    spr.code = sprite_base_ptr[1] & 0x7f;
    spr.color = sprite_base_ptr[2] & 0x0f;
    spr.flags =  ((sprite_base_ptr[2] & 0x80)?1:0) |
      ((sprite_base_ptr[1] & 0x80)?2:0);

    // save sprite in list of active sprites
    if((spr.y > -16) && (spr.y < 288) &&
       (spr.x > -16) && (spr.x < 224))
      sprite[active_sprites++] = spr;
  }
}
#endif

#ifdef ENABLE_FROGGER
void frogger_prepare_frame(void) {
  active_sprites = 0;
  // frogger supports a total of 8 sprites of 8x8 size
  for(int idx=7;idx>=0 && active_sprites < 92;idx--) {
    // sprites are stored at 0x0c40
    unsigned char *sprite_base_ptr = memory + 0xc40 + 4*idx;
    struct sprite_S spr;     

    if(sprite_base_ptr[3]) {
      spr.x = sprite_base_ptr[0];
      spr.x = (((spr.x << 4) & 0xf0) | ((spr.x >> 4) & 0x0f)) - 16;
      spr.y = sprite_base_ptr[3] + 16;
      spr.color = sprite_base_ptr[2] & 7;
      spr.color = ((spr.color >> 1) & 0x03) | ((spr.color << 2) & 0x04);
      spr.code = sprite_base_ptr[1] & 0x3f;
      spr.flags =  ((sprite_base_ptr[1] & 0x80)?1:0) | ((sprite_base_ptr[1] & 0x80)?2:0);

      if((spr.y > -16) && (spr.y < 288) && (spr.x > -16) && (spr.x < 224))
      	sprite[active_sprites++] = spr;
    }    
  }
}
#endif

#ifdef ENABLE_GALAGA
void render_stars_set(short row, const struct galaga_star *set) {    
  for(char star_cntr = 0;star_cntr < 63 ;star_cntr++) {
    const struct galaga_star *s = set+star_cntr;

    unsigned short x = (244 - s->x) & 0xff;
    unsigned short y = ((s->y + stars_scroll_y) & 0xff) + 16 - row * 8;

    if(y < 8 && x < 224)
      frame_buffer[224*y + x] = s->col;
  }     
}
#endif

// draw a single 8x8 tile
void blit_tile(short row, char col) {
  unsigned short addr = tileaddr[row][col];
  const unsigned short *tile, *colors;

#ifdef ENABLE_PACMAN
PACMAN_BEGIN
  {
    tile = pacman_5e[memory[addr]];
    colors = pacman_colormap[memory[0x400 + addr] & 63];
  } 
PACMAN_END  
#endif
  
#ifdef ENABLE_GALAGA
GALAGA_BEGIN
  {
    // skip blank galaga tiles (0x24) in rendering  
    if(memory[addr] == 0x24) return;
    tile = gg1_9_4l[memory[addr]];
    colors = galaga_colormap_tiles[memory[0x400 + addr] & 63];  
  } 
GALAGA_END  
#endif

#ifdef ENABLE_DKONG
DKONG_BEGIN
  {
    /* if(machine == MCH_DKONG) */
    if((row < 2) || (row >= 34)) return;    
    // skip blank dkong tiles (0x10) in rendering  
    if(memory[0x1400 + addr] == 0x10) return;   
    tile = v_5h_b_bin[memory[0x1400 + addr]];
    // donkey kong has some sort of global color table
    colors = dkong_colormap[colortable_select][row-2 + 32*(col/4)];
  }
DKONG_END
#endif

#ifdef ENABLE_FROGGER
FROGGER_BEGIN
  {
    if((row < 2) || (row >= 34))
      return;

    tile = frogger_606[memory[0x0800 + addr]];

    // frogger has a very reduced color handling
    int c = memory[0xc00 + 2 * (addr & 31) + 1] & 7;
    colors = frogger_colormap[((c >> 1) & 0x03) | ((c << 2) & 0x04)];
  }
FROGGER_END
#endif

  unsigned short *ptr = frame_buffer + 8*col;

  // 8 pixel rows per tile
  for(char r=0;r<8;r++,ptr+=(224-8)) {
    unsigned short pix = *tile++;
    // 8 pixel columns per tile
    for(char c=0;c<8;c++,pix>>=2) {
      if(pix & 3) *ptr = colors[pix&3];
      ptr++;
    }
  }
}

#ifdef ENABLE_FROGGER
// frogger can scroll single lines
void blit_tile_scroll(short row, signed char col, short scroll) {
  if((row < 2) || (row >= 34))
    return;

  unsigned short addr;
  unsigned short mask = 0xffff;
  int sub = scroll & 0x07;
  if(col >= 0) {
    addr = tileaddr[row][col];

    // one tile (8 pixels) further is an address offset of 32
    addr = (addr + ((scroll & ~7) << 2)) & 1023;

    if((sub != 0) && (col == 27))
      mask = 0xffff >> (2*sub);    
  } else {
    // negative column is a special case for the leftmost
    // tile when it's only partly visible
    addr = tileaddr[row][0];
    addr = (addr + 32 + ((scroll & ~7) << 2)) & 1023;

    mask = 0xffff << (2*(8-sub));
  }
    
  const unsigned char chr = memory[0x0800 + addr];
  const unsigned short *tile = frogger_606[chr];

  // frogger has a very reduced color handling
  int c = memory[0xc00 + 2 * (addr & 31) + 1] & 7;
  const unsigned short *colors =
    frogger_colormap[((c >> 1) & 0x03) | ((c << 2) & 0x04)];

  unsigned short *ptr = frame_buffer + 8*col + sub;

  // 8 pixel rows per tile
  for(char r=0;r<8;r++,ptr+=(224-8)) {
    unsigned short pix = *tile++ & mask;
    // 8 pixel columns per tile
    for(char c=0;c<8;c++,pix>>=2) {      
      if(pix & 3) *ptr = colors[pix&3];
      ptr++;      
    }
  }
}
#endif

#if defined(ENABLE_PACMAN) || defined(ENABLE_GALAGA)
// render a single 16x16 sprite. This is called multiple times for
// double sized sprites. This renders onto a single 224 x 8 tile row
// thus will be called multiple times even for single sized sprites
void blit_sprite(short row, unsigned char s) {
  const unsigned long *spr;
  const unsigned short *colors;

#ifdef ENABLE_PACMAN
PACMAN_BEGIN
  {
    spr = pacman_sprites[sprite[s].flags & 3][sprite[s].code];
    colors = pacman_colormap[sprite[s].color & 63];
  }
PACMAN_END
#endif

#ifdef ENABLE_GALAGA
GALAGA_BEGIN
  {
    spr = galaga_sprites[sprite[s].flags & 3][sprite[s].code];
    colors = galaga_colormap_sprites[sprite[s].color & 63];
    if(colors[0] != 0) return;   // not a valid colormap entry
  }
GALAGA_END
#endif

  // create mask for sprites that clip left or right
  unsigned long mask = 0xffffffff;
  if(sprite[s].x < 0)      mask <<= -2*sprite[s].x;
  if(sprite[s].x > 224-16) mask >>= (2*(sprite[s].x-(224-16)));		

  short y_offset = sprite[s].y - 8*row;

  // check if there are less than 8 lines to be drawn in this row
  unsigned char lines2draw = 8;
  if(y_offset < -8) lines2draw = 16+y_offset;

  // check which sprite line to begin with
  unsigned short startline = 0;
  if(y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  // if we are not starting to draw with the first line, then
  // skip into the sprite image
  if(y_offset < 0)
    spr -= y_offset;  

  // calculate pixel lines to paint  
  unsigned short *ptr = frame_buffer + sprite[s].x + 224*startline;
  
  // 16 pixel rows per sprite
  for(char r=0;r<lines2draw;r++,ptr+=(224-16)) {
    unsigned long pix = *spr++ & mask;
    // 16 pixel columns per tile
    for(char c=0;c<16;c++,pix>>=2) {
      unsigned short col = colors[pix&3];
      if(col) *ptr = col;
      ptr++;
    }
  }
}
#endif

#ifdef ENABLE_DKONG
// dkong has its own sprite drawing routine since unlike the other
// games, in dkong black is not always transparent. Black pixels
// are instead used for masking
void blit_sprite_dkong(short row, unsigned char s) {
  const unsigned long *spr = dkong_sprites[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = dkong_colormap_sprite[colortable_select][sprite[s].color];
  
  // create mask for sprites that clip left or right
  unsigned long mask = 0xffffffff;
  if(sprite[s].x < 0)      mask <<= -2*sprite[s].x;
  if(sprite[s].x > 224-16) mask >>= 2*(sprite[s].x-224-16);    

  short y_offset = sprite[s].y - 8*row;

  // check if there are less than 8 lines to be drawn in this row
  unsigned char lines2draw = 8;
  if(y_offset < -8) lines2draw = 16+y_offset;

  // check which sprite line to begin with
  unsigned short startline = 0;
  if(y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  // if we are not starting to draw with the first line, then
  // skip into the sprite image
  if(y_offset < 0)
    spr -= y_offset;  

  // calculate pixel lines to paint  
  unsigned short *ptr = frame_buffer + sprite[s].x + 224*startline;
  
  // 16 pixel rows per sprite
  for(char r=0;r<lines2draw;r++,ptr+=(224-16)) {
    unsigned long pix = *spr++ & mask;
    // 16 pixel columns per tile
    for(char c=0;c<16;c++,pix>>=2) {
      unsigned short col = colors[pix&3];
      if(pix & 3) *ptr = col;
      ptr++;
    }
  }
}
#endif

#ifdef ENABLE_FROGGER
void blit_sprite_frogger(short row, unsigned char s) {
  const unsigned long *spr = frogger_sprites[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = frogger_colormap[sprite[s].color];
  
  // create mask for sprites that clip left or right
  unsigned long mask = 0xffffffff;
  if(sprite[s].x < 0)      mask <<= -2*sprite[s].x;
  if(sprite[s].x > 224-16) mask >>= 2*(sprite[s].x-224-16);		

  short y_offset = sprite[s].y - 8*row;

  // check if there are less than 8 lines to be drawn in this row
  unsigned char lines2draw = 8;
  if(y_offset < -8) lines2draw = 16+y_offset;

  // check which sprite line to begin with
  unsigned short startline = 0;
  if(y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  // if we are not starting to draw with the first line, then
  // skip into the sprite image
  if(y_offset < 0) spr -= y_offset;  

  // calculate pixel lines to paint  
  unsigned short *ptr = frame_buffer + sprite[s].x + 224*startline;
  
  // 16 pixel rows per sprite
  for(char r=0;r<lines2draw;r++,ptr+=(224-16)) {
    unsigned long pix = *spr++ & mask;
    // 16 pixel columns per tile
    for(char c=0;c<16;c++,pix>>=2) {
      unsigned short col = colors[pix&3];
      if(pix & 3) *ptr = col;
      ptr++;
    }
  }
}
#endif

#ifndef SINGLE_MACHINE
// convert rgb565 big endian color to greyscale
unsigned short greyscale(unsigned short in) {
  unsigned short r = (in>>3) & 31;
  unsigned short g = ((in<<3) & 0x38) | ((in>>13)&0x07);
  unsigned short b = (in>>8)& 31;
  unsigned short avg = (2*r + g + 2*b)/4;
  
  return (((avg << 13) & 0xe000) |   // g2-g0
          ((avg <<  7) & 0x1f00) |   // b5-b0
          ((avg <<  2) & 0x00f8) |   // r5-r0
          ((avg >>  3) & 0x0007));   // g5-g3
}

// render one of three the menu logos. Only the active one is colorful
// render logo into current buffer starting with line "row" of the logo
void render_logo(short row, const unsigned short *logo, char active) {
  unsigned short marker = logo[0];
  const unsigned short *data = logo+1;

  // current pixel to be drawn
  unsigned short ipix = 0;
    
  // less than 8 rows in image left?
  unsigned short pix2draw = ((row <= 96-8)?(224*8):((96-row)*224));
  
  if(row >= 0) {
    // skip ahead to row
    unsigned short col = 0;
    unsigned short pix = 0;
    while(pix < 224*row) {
      if(data[0] != marker) {
        pix++;
        data++;
      } else {
        pix += data[1]+1;
        col = data[2];
        data += 3;
      }
    }
    
    // draw pixels remaining from previous run
    if(!active) col = greyscale(col);
    while(ipix < ((pix - 224*row < pix2draw)?(pix - 224*row):pix2draw))
      frame_buffer[ipix++] = col;
  } else
    // if row is negative, then skip target pixel
    ipix -= row * 224;
    
  while(ipix < pix2draw) {
    if(data[0] != marker)
      frame_buffer[ipix++] = active?*data++:greyscale(*data++);
    else {
      unsigned short color = data[2];
      if(!active) color = greyscale(color);
      for(unsigned short j=0;j<data[1]+1 && ipix < pix2draw;j++)
        frame_buffer[ipix++] = color;

      data += 3;
    }
  }  
}
#endif

#ifndef SINGLE_MACHINE
// menu for more than three machines
const unsigned short *logos[] = {
#ifdef ENABLE_PACMAN    
  pacman_logo,
#endif
#ifdef ENABLE_GALAGA
  galaga_logo,
#endif
#ifdef ENABLE_DKONG    
  dkong_logo,
#endif
#ifdef ENABLE_FROGGER    
  frogger_logo,
#endif
};
#endif

// render one of 36 tile rows (8 x 224 pixel lines)
void render_line(short row) {
  // the upper screen half of frogger has a blue background
  // using 8 in fact adds a tiny fraction of red as well. But that does not hurt
  memset(frame_buffer, 
#ifdef ENABLE_FROGGER 
    (MACHINE_IS_FROGGER && row <= 17)?8:
#endif
    0, 2*224*8);

#ifndef SINGLE_MACHINE
  if(machine == MCH_MENU) {

#ifndef MENU_SCROLL
    // non-scrolling menu for 2 or 3 machines
    for(char i=0;i<sizeof(logos)/sizeof(unsigned short*);i++) {
      char offset = i*12;
      if(sizeof(logos)/sizeof(unsigned short*) == 2) offset += 6;
      
      if(row >= offset && row < offset+12)  
        render_logo(8*(row-offset), logos[i], menu_sel == i+1);
    }
#else // MENU_SCROLL
    // scrolling menu for more than 3 machines
    
    // valid offset values range from 0 to MACHINE*96-1
    static int offset = 0;

    // check which logo would show up in this row. Actually
    // two may show up in the same character row when scrolling
    int logo_idx = ((row + offset/8) / 12)%MACHINES;
    if(logo_idx < 0) logo_idx += MACHINES;
    
    int logo_y = (row * 8 + offset)%96;  // logo line in this row

    // check if logo at logo_y shows up in current row
    render_logo(logo_y, logos[logo_idx], (menu_sel-1) == logo_idx);

    // check if a second logo may show up here
    if(logo_y > (96-8)) {
        logo_idx = (logo_idx + 1)%MACHINES;
        logo_y -= 96;
        render_logo(logo_y, logos[logo_idx], (menu_sel-1) == logo_idx);
    }
    
    if(row == 35) {
      // finally offset is bound to game, something like 96*game:    
      int new_offset = 96*((unsigned)(menu_sel-2)%MACHINES);

      // check if we need to scroll
      if(new_offset != offset) {
        int diff = (new_offset - offset) % (MACHINES*96);
        if(diff < 0) diff += MACHINES*96;
  
        if(diff < MACHINES*96/2) offset = (offset+8)%(MACHINES*96);
        else                     offset = (offset-8)%(MACHINES*96);
        if(offset < 0) offset += MACHINES*96;
      }
    }  
#endif
  } else
#endif  

#ifdef ENABLE_PACMAN
PACMAN_BEGIN
  {
    // render 28 tile columns per row
    for(char col=0;col<28;col++)
      blit_tile(row, col);
    
    // render sprites
    for(unsigned char s=0;s<active_sprites;s++) {
      // check if sprite is visible on this row
      if((sprite[s].y < 8*(row+1)) && ((sprite[s].y+16) > 8*row))
        blit_sprite(row, s);
    }
  }
PACMAN_END
#endif
  
#ifdef ENABLE_GALAGA
GALAGA_BEGIN
  {
    if(starcontrol & 0x20) {
      /* two sets of stars controlled by these bits */
      render_stars_set(row, galaga_star_set[(starcontrol & 0x08)?1:0]);
      render_stars_set(row, galaga_star_set[(starcontrol & 0x10)?3:2]);
    }

    // render sprites
    for(unsigned char s=0;s<active_sprites;s++) {
      // check if sprite is visible on this row
      if((sprite[s].y < 8*(row+1)) && ((sprite[s].y+16) > 8*row))
        blit_sprite(row, s);
    }

    // render 28 tile columns per row
    for(char col=0;col<28;col++)
      blit_tile(row, col);
  } 
GALAGA_END
#endif

#ifdef ENABLE_DKONG
DKONG_BEGIN
  {
    // render 28 tile columns per row
    for(char col=0;col<28;col++)
        blit_tile(row, col);
    
    // render sprites
    for(unsigned char s=0;s<active_sprites;s++) {
      // check if sprite is visible on this row
      if((sprite[s].y < 8*(row+1)) && ((sprite[s].y+16) > 8*row))
        blit_sprite_dkong(row, s);
    }
  }
DKONG_END
#endif

#ifdef ENABLE_FROGGER
FROGGER_BEGIN
  {
    // don't render lines 0, 1, 34 and 35
    if(row <= 1 || row >= 34) return;

    // get scroll info for this row
    unsigned char scroll = memory[0xc00 + 2 * (row - 2)];
    scroll = ((scroll << 4) & 0xf0) | ((scroll >> 4) & 0x0f);

    // in frogger scroll will only affect rows
    // water:  8/ 9, 10/11, 12/13, 14/15, 16/17
    // road:  20/21, 22/23, 24/25, 26/27, 28/29

    // render 28 tile columns per row. Handle frogger specific
    // scroll capabilities
    if(scroll == 0) // no scroll in this line?
      for(char col=0;col<28;col++)
    	  blit_tile(row, col);
    else {
      // if scroll offset is multiple of 8, then
      // 28 tiles are sufficient, otherwise the first
      // fragment needs to be drawn
      if(scroll & 7) 
      	blit_tile_scroll(row, -1, scroll);

      for(char col=0;col<28;col++)
	      blit_tile_scroll(row, col, scroll);
    }
    // render sprites
    for(unsigned char s=0;s<active_sprites;s++) {
      // check if sprite is visible on this row
      if((sprite[s].y < 8*(row+1)) && ((sprite[s].y+16) > 8*row))
	      blit_sprite_frogger(row, s);
    }
  }
FROGGER_END
#endif
}

#ifdef ENABLE_GALAGA
void galaga_trigger_sound_explosion(void) {
  if(game_started) {
    snd_boom_cnt = 2*sizeof(galaga_sample_boom);
    snd_boom_ptr = (const signed char*)galaga_sample_boom;
  }
}
#endif

#if defined(ENABLE_PACMAN) || defined(ENABLE_GALAGA)
static unsigned long snd_cnt[3] = { 0,0,0 };
static unsigned long snd_freq[3];
static const signed char *snd_wave[3];
static unsigned char snd_volume[3];
#endif

#ifdef SND_DIFF
static unsigned short snd_buffer[128]; // buffer space for two channels
#else
static unsigned short snd_buffer[64];  // buffer space for a single channel
#endif

void snd_render_buffer(void) {
#ifdef ENABLE_FROGGER
#ifndef SINGLE_MACHINE
  if(machine == MCH_FROGGER)
#endif
  {
    // check if soundregisters have changed
    char same = 1;
    for(char i=0;i<14;i++)
      if(soundregs[i] != soundregs[16+i])
      	same = 0;

    if(!same) {
      // recalc audio state
      for(char c=0;c<3;c++) {	
      	ay_period[c] = soundregs[2*c] + 256 * (soundregs[2*c+1] & 15);
	      ay_enable[c] = !(soundregs[7] & (1<<c));
      	ay_volume[c] = soundregs[8+c] & 0x0f;

        // printf("AY %d %d\n", c, ay_period[c]);
      }

      for(char i=0;i<14;i++)
	      soundregs[16+i] = soundregs[i];
    }
  }
#endif

  // render first buffer contents
  for(int i=0;i<64;i++) {
    short v = 0;

#if defined(ENABLE_PACMAN) || defined(ENABLE_GALAGA)
  #ifndef SINGLE_MACHINE
    if(0
    #ifdef ENABLE_PACMAN
        || (machine == MCH_PACMAN)
    #endif
    #ifdef ENABLE_GALAGA
        || (machine == MCH_GALAGA)
    #endif
    ) 
  #endif
    {
      // add up to three wave signals
      if(snd_volume[0]) v += snd_volume[0] * snd_wave[0][(snd_cnt[0]>>14) & 0x1f];
      if(snd_volume[1]) v += snd_volume[1] * snd_wave[1][(snd_cnt[1]>>14) & 0x1f];
      if(snd_volume[2]) v += snd_volume[2] * snd_wave[2][(snd_cnt[2]>>14) & 0x1f];

  #ifdef ENABLE_GALAGA
      if(snd_boom_cnt) {
        v += *snd_boom_ptr;
        if(snd_boom_cnt & 1) snd_boom_ptr++;
        snd_boom_cnt--;
      }
  #endif
    }
#endif    

#ifdef ENABLE_DKONG
DKONG_BEGIN
    {
      v = 0;  // silence

      // no buffer available
      if(dkong_audio_rptr != dkong_audio_wptr)
        // copy data from dkong buffer into tx buffer
        // 8048 sounds gets 50% of the available volume range
        v = dkong_audio_transfer_buffer[dkong_audio_rptr][i];
          
      // include sample sounds
      // walk is 6.25% volume, jump is at 12.5% volume and, stomp is at 25%
      for(char i=0;i<3;i++) {
        if(dkong_sample_cnt[i]) {
          v += *dkong_sample_ptr[i]++ >> (2-i); 
          dkong_sample_cnt[i]--;
        }
      }
    }
DKONG_END
#endif

#ifdef ENABLE_FROGGER
FROGGER_BEGIN
    {
      v = 0;  // silence

      for(char c=0;c<3;c++) {      
	      // a channel is on if period != 0, vol != 0 and tone bit == 0
	      if(ay_period[c] && ay_volume[c] && ay_enable[c]) {
	        v += 11 * audio_toggle[c] * ay_volume[c];  // min/max = -/+ 45*11 = -/+ 495
	        audio_cnt[c] += 9; // for 24 khz
	        if(audio_cnt[c] > ay_period[c]) {
	          audio_cnt[c] -= ay_period[c];
	          audio_toggle[c] = -audio_toggle[c];
	        }
        }
      }
    }
FROGGER_END
#endif
    // v is now in the range of +/- 512, so expand to +/- 15 bit
    v = v*64;

#ifdef SND_DIFF
    // generate differential output
    snd_buffer[2*i]   = 0x8000 + v;    // positive signal on GPIO26
    snd_buffer[2*i+1] = 0x8000 - v;    // negatve signal on GPIO25 
#else
    snd_buffer[i]     = 0x8000 + v;
#endif
      
#if defined(ENABLE_PACMAN) || defined(ENABLE_GALAGA)
    snd_cnt[0] += snd_freq[0];
    snd_cnt[1] += snd_freq[1];
    snd_cnt[2] += snd_freq[2];
#endif
  }
  
#ifdef ENABLE_DKONG
  #ifndef SINGLE_MACHINE
  if(machine == MCH_DKONG)
  #endif
  {
    // advance write pointer. The buffer is a ring
    dkong_audio_rptr = (dkong_audio_rptr+1)&DKONG_AUDIO_QUEUE_MASK;
      
    if(dkong_audio_rptr == dkong_audio_wptr)
      printf("DK audio buffer emptied\n");
  }
#endif
}

#if defined(ENABLE_PACMAN) || defined(ENABLE_GALAGA)
void audio_namco_waveregs_parse(void) {
#ifndef SINGLE_MACHINE
  if(0
  #ifdef ENABLE_PACMAN
    || (machine == MCH_PACMAN)
  #endif
  #ifdef ENABLE_GALAGA
    || (machine == MCH_GALAGA)
  #endif
  )
#endif   
  {
    // parse all three wsg channels
    for(char ch=0;ch<3;ch++) {  
      // channel volume
      snd_volume[ch] = soundregs[ch * 5 + 0x15];
    
      if(snd_volume[ch]) {
        // frequency
        snd_freq[ch] = (ch == 0) ? soundregs[0x10] : 0;
        snd_freq[ch] += soundregs[ch * 5 + 0x11] << 4;
        snd_freq[ch] += soundregs[ch * 5 + 0x12] << 8;
        snd_freq[ch] += soundregs[ch * 5 + 0x13] << 12;
        snd_freq[ch] += soundregs[ch * 5 + 0x14] << 16;
      
        // wavetable entry
#ifdef ENABLE_PACMAN
  #ifdef ENABLE_GALAGA
        if(machine == MCH_PACMAN)
  #endif
          snd_wave[ch] = pacman_wavetable[soundregs[ch * 5 + 0x05] & 0x0f];
  #ifdef ENABLE_GALAGA
        else
  #endif      
#endif      
#ifdef ENABLE_GALAGA
          snd_wave[ch] = galaga_wavetable[soundregs[ch * 5 + 0x05] & 0x07];
#endif      
      }
    }
  }
}
#endif  // MCH_PACMAN || MCH_GALAGA

#ifdef ENABLE_DKONG
void dkong_trigger_sound(char snd) {
  static const struct {
    const signed char *data;
    const unsigned short length; 
  } samples[] = {
    { (const signed char *)dkong_sample_walk0, sizeof(dkong_sample_walk0) },
    { (const signed char *)dkong_sample_walk1, sizeof(dkong_sample_walk1) },
    { (const signed char *)dkong_sample_walk2, sizeof(dkong_sample_walk2) },
    { (const signed char *)dkong_sample_jump,  sizeof(dkong_sample_jump)  },
    { (const signed char *)dkong_sample_stomp, sizeof(dkong_sample_stomp) }
  };

  // samples 0 = walk, 1 = jump, 2 = stomp

  if(!snd) {
    // walk0, walk1 and walk2 are variants
    char rnd = random() % 3;
    dkong_sample_cnt[0] = samples[rnd].length;
    dkong_sample_ptr[0] = samples[rnd].data;
  } else {
    dkong_sample_cnt[snd] = samples[snd+2].length;
    dkong_sample_ptr[snd] = samples[snd+2].data;
  }
}
#endif

void snd_transmit() {
  // (try to) transmit as much audio data as possible. Since we
  // write data in exact the size of the DMA buffers we can be sure
  // that either all or nothing is actually being written
  
  size_t bytesOut = 0;
  do {
    // copy data in i2s dma buffer if possible
    i2s_write(I2S_NUM_0, snd_buffer, sizeof(snd_buffer), &bytesOut, 0);

    // render the next audio chunk if data has actually been sent
    if(bytesOut) {
#if defined(ENABLE_PACMAN) || defined(ENABLE_GALAGA)
      audio_namco_waveregs_parse();
#endif
      snd_render_buffer();
    }
  } while(bytesOut);
}

void audio_dkong_bitrate(char is_dkong) {
  // The audio CPU of donkey kong runs at 6Mhz. A full bus
  // cycle needs 15 clocks which results in 400k cycles
  // per second. The sound CPU typically needs 34 instruction
  // cycles to write an updated audio value to the external
  // DAC connected to port 0.
  
  // The effective sample rate thus is 6M/15/34 = 11764.7 Hz
  i2s_set_sample_rates(I2S_NUM_0, is_dkong?11765:24000);
}

void audio_init(void) {  
  // init audio
#if defined(ENABLE_PACMAN) || defined(ENABLE_GALAGA)
  audio_namco_waveregs_parse();
#endif
  snd_render_buffer();

  // 24 kHz @ 16 bit = 48000 bytes/sec = 800 bytes per 60hz game frame =
  // 1600 bytes per 30hz screen update = ~177 bytes every four tile rows
  static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
    .sample_rate = 24000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
#ifdef SND_DIFF
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
#else
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
#endif
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 64,   // 64 samples
    .use_apll = true
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);

#if defined(SINGLE_MACHINE) && defined(ENABLE_DKONG)
  // only dkong installed? Then setup rate immediately
  audio_dkong_bitrate(true);
#endif

#ifdef SND_DIFF
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
#else
  i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);
#endif
}

void update_screen(void) {
  uint32_t t0 = micros();

#ifdef ENABLE_PACMAN
  #ifndef SINGLE_MACHINE
  if(machine == MCH_PACMAN)
  #endif
    pacman_prepare_frame();    
  #ifndef SINGLE_MACHINE
    else
  #endif
#endif

#ifdef ENABLE_GALAGA
  #ifdef ENABLE_DKONG
  if(machine == MCH_GALAGA)
  #endif
    galaga_prepare_frame();
  #ifdef ENABLE_DKONG
  else
  #endif
#endif  
  
#ifdef ENABLE_DKONG
  #ifndef SINGLE_MACHINE
  if(machine == MCH_DKONG)
  #endif
    dkong_prepare_frame();
#endif

#ifdef ENABLE_FROGGER
  #ifndef SINGLE_MACHINE
  if(machine == MCH_FROGGER)
  #endif
    frogger_prepare_frame();
#endif

  // max possible video rate:
  // 8*224 pixels = 8*224*16 = 28672 bits
  // 2790 char rows per sec at 40Mhz = max 38 fps
#if TFT_SPICLK < 80000000
#define VIDEO_HALF_RATE
#endif

#ifdef VIDEO_HALF_RATE
  // render and transmit screen in two halfs as the display
  // running at 40Mhz can only update every second 60 hz game frame
  for(int half=0;half<2;half++) {

    for(int c=18*half;c<18*(half+1);c+=3) {
      render_line(c+0); tft.write(frame_buffer, 224*8);
      render_line(c+1); tft.write(frame_buffer, 224*8);
      render_line(c+2); tft.write(frame_buffer, 224*8);

      // audio is refilled 6 times per screen update. The screen is updated
      // every second frame. So audio is refilled 12 times per 30 Hz frame.
      // Audio registers are udated by CPU3 two times per 30hz frame.
      snd_transmit();
    } 
 
    // one screen at 60 Hz is 16.6ms
    unsigned long t1 = (micros()-t0)/1000;  // calculate time in milliseconds
    // printf("uspf %d\n", t1);
    if(t1<(half?33:16)) vTaskDelay((half?33:16)-t1);
    else if(half)       vTaskDelay(1);    // at least 1 ms delay to prevent watchdog timeout

    // physical refresh is 30Hz. So send vblank trigger twice a frame
    // to the emulation. This will make the game run with 60hz speed
    xTaskNotifyGive(emulationtask);
  }
#else
  #warning FULL SPEED
  
  // render and transmit screen at once as the display
  // running at 80Mhz can update at full 60 hz game frame
  for(int c=0;c<36;c+=6) {
    render_line(c+0); tft.write(frame_buffer, 224*8);
    render_line(c+1); tft.write(frame_buffer, 224*8);
    render_line(c+2); tft.write(frame_buffer, 224*8);
    render_line(c+3); tft.write(frame_buffer, 224*8);
    render_line(c+4); tft.write(frame_buffer, 224*8);
    render_line(c+5); tft.write(frame_buffer, 224*8);

    // audio is updated 6 times per 60 Hz frame
    snd_transmit();
  } 
 
  // one screen at 60 Hz is 16.6ms
  unsigned long t1 = (micros()-t0)/1000;  // calculate time in milliseconds
//  printf("uspf %d\n", t1);
  if(t1<16) vTaskDelay(16-t1);
  else      vTaskDelay(1);    // at least 1 ms delay to prevent watchdog timeout

  // physical refresh is 60Hz. So send vblank trigger once a frame
  xTaskNotifyGive(emulationtask);
#endif
   
#ifdef ENABLE_GALAGA
  /* the screen is only updated every second frame, scroll speed is thus doubled */
  static const signed char speeds[8] = { -1, -2, -3, 0, 3, 2, 1, 0 };
  stars_scroll_y += 2*speeds[starcontrol & 7];
#endif
}

void emulation_task(void *p) {
  prepare_emulation();

  while(1)
    emulate_frame();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Galagino"); 

  // this should not be needed as the CPU runs by default on 240Mht nowadays
  setCpuFrequencyMhz(240000000);

  Serial.print("Free heap: "); Serial.println(ESP.getFreeHeap());
  Serial.print("Main core: "); Serial.println(xPortGetCoreID());
  Serial.print("Main priority: "); Serial.println(uxTaskPriorityGet(NULL));  

  // allocate memory for a single tile/character row
  frame_buffer = (unsigned short*)malloc(224*8*2);
  sprite = (struct sprite_S*)malloc(96 * sizeof(struct sprite_S));
  Serial.print("Free heap: "); Serial.println(ESP.getFreeHeap());

  // make button pins inputs
  pinMode(BTN_START_PIN, INPUT_PULLUP);
#ifdef BTN_COIN_PIN
  pinMode(BTN_COIN_PIN, INPUT_PULLUP);
#endif
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_FIRE_PIN, INPUT_PULLUP);

  // initialize audio to default bitrate (24khz unless dkong is
  // the inly game installed, then audio will directly be 
  // initialized to dkongs 11765hz)
  audio_init();

#ifdef LED_PIN
  leds_init();
#endif

  // let the cpu emulation run on the second core, so the main core
  // can completely focus on video
  xTaskCreatePinnedToCore(
      emulation_task, /* Function to implement the task */
      "emulation task", /* Name of the task */
      4096,  /* Stack size in words */
      NULL,  /* Task input parameter */
      2,  /* Priority of the task */
      &emulationtask,  /* Task handle. */
      0); /* Core where the task should run */

  tft.begin();
}

unsigned char buttons_get(void) {
  // galagino can be compiled without coin button. This will then
  // be implemented by the start button. Whenever the start button 
  // is pressed, a virtual coin button will be sent first 
#ifndef BTN_COIN_PIN
  static unsigned long virtual_coin_timer = 0;
  static int virtual_coin_state = 0;
  switch(virtual_coin_state)  {
    case 0:  // idle state
      if(!digitalRead(BTN_START_PIN)) {
        virtual_coin_state = 1;   // virtual coin pressed
        virtual_coin_timer = millis();
      }
      break;
    case 1:  // start was just pressed
      // check if 100 milliseconds have passed
      if(millis() - virtual_coin_timer > 100) {
        virtual_coin_state = 2;   // virtual coin released
        virtual_coin_timer = millis();        
      }
      break;
    case 2:  // virtual coin was released
      // check if 500 milliseconds have passed
      if(millis() - virtual_coin_timer > 500) {
        virtual_coin_state = 3;   // pause between virtual coin an start ended
        virtual_coin_timer = millis();        
      }
      break;
    case 3:  // pause ended
      // check if 100 milliseconds have passed
      if(millis() - virtual_coin_timer > 100) {
        virtual_coin_state = 4;   // virtual start ended
        virtual_coin_timer = millis();        
      }
      break;
    case 4:  // virtual start has ended
      // check if start button is actually still pressed
      if(digitalRead(BTN_START_PIN))
        virtual_coin_state = 0;   // button has been released, return to idle
      break;
  }
#endif

#ifndef SINGLE_MACHINE
  static unsigned long reset_timer = 0;
  
  // reset if coin (or start if no coin is configured) is held for
  // more than 1 second
  if(!digitalRead(
#ifdef BTN_COIN_PIN
    BTN_COIN_PIN
#else
    BTN_START_PIN
#endif
  )) {
    if(machine != MCH_MENU) {

#ifdef MASTER_ATTRACT_GAME_TIMEOUT
       // if the game was started by the master attract mode then the user
       // pressing coin (or start) stops the timer, so the game keeps
       // running as long as the user wants
       master_attract_timeout = 0;
#endif

      if(!reset_timer)
        reset_timer = millis();

      if(millis() - reset_timer > 1000) {
        // disable backlight if pin is specified
#ifdef TFT_BL
        digitalWrite(TFT_BL, LOW);
#endif

        emulation_reset();
      }
    }    
  } else
    reset_timer = 0;
#endif
  
  return 
#ifdef BTN_COIN_PIN
    // there is a coin pin -> coin and start work normal
    (digitalRead(BTN_START_PIN)?0:BUTTON_START) |
    (digitalRead(BTN_COIN_PIN)?0:BUTTON_COIN) |
#else
    ( (virtual_coin_state != 1)?0:BUTTON_COIN) |
    (((virtual_coin_state != 3)&&(virtual_coin_state != 4))?0:BUTTON_START) |
#endif
    (digitalRead(BTN_LEFT_PIN)?0:BUTTON_LEFT) |
    (digitalRead(BTN_RIGHT_PIN)?0:BUTTON_RIGHT) |
    (digitalRead(BTN_UP_PIN)?0:BUTTON_UP) |
    (digitalRead(BTN_DOWN_PIN)?0:BUTTON_DOWN) |
    (digitalRead(BTN_FIRE_PIN)?0:BUTTON_FIRE);
}

void loop(void) {
  // run video in main task. This will send signals
  // to the emulation task in the background to 
  // synchronize video
  update_screen(); 

#ifdef LED_PIN
  leds_update();
#endif
}
