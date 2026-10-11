#include "mouse.h"
#include "editor_state.h"
#include "screen.h"
#include <stdio.h>

// CIA1 and SID registers. Volatile so the port-A write stays ahead of the port-B read.
#define CIA1_PRA (*(volatile unsigned char*)0xDC00)
#define CIA1_PRB (*(volatile unsigned char*)0xDC01)
#define CIA1_DDRA (*(volatile unsigned char*)0xDC02)
#define CIA1_DDRB (*(volatile unsigned char*)0xDC03)
#define SID_POTX (*(volatile unsigned char*)0xD419)
#define SID_POTY (*(volatile unsigned char*)0xD41A)

/* Port 1 ($DC01) is active low. Bit 0 is also the 1351 right button.
   The main loop is not a frame, so a held direction repeats every 4 jiffies. */
#define JOY_UP     0x01
#define JOY_DOWN   0x02
#define JOY_LEFT   0x04
#define JOY_RIGHT  0x08
#define JOY_REPEAT 4
/* Port 1 pulls a keyboard row low. SCNKEY will queue that row's key.
   Swallow the buffer while a line is down, and for a few jiffies after
   it rises, so the key queued on the way up is dropped too. */
#define JOY_KEY_GUARD 8
#define KBD_COUNT (*(volatile unsigned char*)0x00C6)
#define JIFFY_LO (*(volatile unsigned char*)0x00A2)

// Mouse cursor character
#define MOUSE_CURSOR 94

// Mouse sensitivity (higher = slower). Try values between 16-32
#define MOUSE_DIVISOR 24

static MouseState mouse;
static unsigned char mouse_visible = 0;
static unsigned char old_char = ' ';
static unsigned char old_color = COL_WHITE;
static unsigned char old_x = 0;
static unsigned char old_y = 0;

// Previous POT values for delta calculation
static unsigned char oldpotx = 0;
static unsigned char oldpoty = 0;
static unsigned char joy_held = 0;
static unsigned char joy_stamp = 0;
static unsigned char joy_guard_on = 0;
static unsigned char joy_guard_stamp = 0;

/* Drive port A high around the read so a keyboard column is not a direction.
   Do not sample port A. It shares the keyboard columns. */
static unsigned char read_port1(void) {
    unsigned char prb;

    __asm__ volatile ("sei" ::: "memory");
    CIA1_PRA = 0xFF;
    prb = CIA1_PRB;
    __asm__ volatile ("cli" ::: "memory");
    return prb;
}

static unsigned char poll_port1(void) {
    unsigned char prb, dir, now;
    int x, y;

    prb = read_port1();
    mouse.prev_button = mouse.button;
    mouse.button = (prb & 0x10) ? MOUSE_BTN_NONE : MOUSE_BTN_LEFT;

    /* Directions and fire are keyboard rows. Remember the jiffy so typed
       ghosts from this line can be dropped after it is released. */
    if ((prb & 0x1F) != 0x1F) {
        joy_guard_on = 1;
        joy_guard_stamp = JIFFY_LO;
    }

    dir = (unsigned char)(~prb) & 0x0F;
    if (dir == 0) {
        joy_held = 0;
        return 0;
    }

    now = JIFFY_LO;
    if (dir == joy_held && (unsigned char)(now - joy_stamp) < JOY_REPEAT)
        return 0;

    joy_held = dir;
    joy_stamp = now;
    x = mouse.x;
    y = mouse.y;
    if (dir & JOY_LEFT) x--;
    if (dir & JOY_RIGHT) x++;
    if (dir & JOY_UP) y--;
    if (dir & JOY_DOWN) y++;
    if (x < 0) x = 0;
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;
    if (x == mouse.x && y == mouse.y)
        return 0;
    mouse.x = (unsigned char)x;
    mouse.y = (unsigned char)y;
    return 1;
}

void mouse_init(void) {
    mouse.enabled = 0;
    mouse.x = 20;
    mouse.y = 12;
    mouse.button = MOUSE_BTN_NONE;
    mouse.prev_button = MOUSE_BTN_NONE;
    oldpotx = 0;
    oldpoty = 0;
    joy_held = 0;
    joy_stamp = 0;
    joy_guard_on = 0;
    mouse_visible = 0;
    old_x = 0;
    old_y = 0;
}

void mouse_update(void) {
    unsigned char newpotx, newpoty;
    signed int deltax, deltay;
    int new_x, new_y;
    unsigned char moved = 0;
    
    if (!mouse.enabled) return;
    
    // Read POT values
    newpotx = SID_POTX;
    newpoty = SID_POTY;
    
    // Calculate raw deltas
    deltax = (signed int)newpotx - (signed int)oldpotx;
    deltay = (signed int)newpoty - (signed int)oldpoty;
    
    // Handle wraparound for 8-bit values
    if (deltax > 127) deltax -= 256;
    if (deltax < -128) deltax += 256;
    if (deltay > 127) deltay -= 256;
    if (deltay < -128) deltay += 256;
    
    // CRITICAL: Ignore huge jumps (likely errors or initialization)
    if (deltax > 50 || deltax < -50) deltax = 0;
    if (deltay > 50 || deltay < -50) deltay = 0;
    
    // Apply movement with scaling
    if (deltax != 0) {
        deltax = deltax / MOUSE_DIVISOR;
        
        // Ensure minimum movement if there was any delta
        if (deltax == 0 && newpotx != oldpotx) {
            // Only move if the difference is significant enough
            int diff = (int)newpotx - (int)oldpotx;
            if (diff > 127) diff -= 256;
            if (diff < -128) diff += 256;
            
            if (diff > 2) deltax = 1;
            else if (diff < -2) deltax = -1;
        }
        
        if (deltax != 0) {
            new_x = (int)mouse.x + deltax;
            if (new_x < 0) new_x = 0;
            if (new_x >= SCREEN_WIDTH) new_x = SCREEN_WIDTH - 1;
            mouse.x = (unsigned char)new_x;
            moved = 1;
        }
    }
    
    // Y axis (inverted)
    if (deltay != 0) {
        deltay = -deltay / MOUSE_DIVISOR;  // Invert and scale
        
        if (deltay == 0 && newpoty != oldpoty) {
            int diff = (int)newpoty - (int)oldpoty;
            if (diff > 127) diff -= 256;
            if (diff < -128) diff += 256;
            
            if (diff > 2) deltay = -1;
            else if (diff < -2) deltay = 1;
        }
        
        if (deltay != 0) {
            new_y = (int)mouse.y + deltay;
            if (new_y < 0) new_y = 0;
            if (new_y >= SCREEN_HEIGHT) new_y = SCREEN_HEIGHT - 1;
            mouse.y = (unsigned char)new_y;
            moved = 1;
        }
    }
    
    if (poll_port1())
        moved = 1;

    // If mouse moved, we need to redraw it
    if (moved && mouse_visible) {
        // Restore old position first
        int old_pos = old_y * SCREEN_WIDTH + old_x;
        SCREEN_RAM[old_pos] = old_char;
        COLOR_RAM[old_pos] = old_color;
        
        // Now draw at new position
        int new_pos = mouse.y * SCREEN_WIDTH + mouse.x;
        old_char = SCREEN_RAM[new_pos];
        old_color = COLOR_RAM[new_pos];
        old_x = mouse.x;
        old_y = mouse.y;
        
        SCREEN_RAM[new_pos] = MOUSE_CURSOR;
        COLOR_RAM[new_pos] = COL_YELLOW;
    }
    
    // Update stored values
    oldpotx = newpotx;
    oldpoty = newpoty;
}

void mouse_hide_cursor(void) {
    if (!mouse.enabled || !mouse_visible) return;
    
    int pos = old_y * SCREEN_WIDTH + old_x;
    SCREEN_RAM[pos] = old_char;
    COLOR_RAM[pos] = old_color;
    
    mouse_visible = 0;
}

void mouse_draw_cursor(void) {
    if (!mouse.enabled) return;
    
    // Don't redraw if already visible and hasn't moved
    if (mouse_visible && old_x == mouse.x && old_y == mouse.y) {
        return;
    }
    
    // If visible but moved, hide old position first
    if (mouse_visible) {
        int old_pos = old_y * SCREEN_WIDTH + old_x;
        SCREEN_RAM[old_pos] = old_char;
        COLOR_RAM[old_pos] = old_color;
    }
    
    // Draw at current position
    int pos = mouse.y * SCREEN_WIDTH + mouse.x;
    old_char = SCREEN_RAM[pos];
    old_color = COLOR_RAM[pos];
    old_x = mouse.x;
    old_y = mouse.y;
    
    SCREEN_RAM[pos] = MOUSE_CURSOR;
    COLOR_RAM[pos] = COL_YELLOW;
    
    mouse_visible = 1;
}

unsigned char mouse_get_click(void) {
    if (mouse.button != MOUSE_BTN_NONE && 
        mouse.prev_button == MOUSE_BTN_NONE) {
        return mouse.button;
    }
    return MOUSE_BTN_NONE;
}

void mouse_get_position(unsigned char *x, unsigned char *y) {
    *x = mouse.x;
    *y = mouse.y;
}

unsigned char mouse_is_enabled(void) {
    return mouse.enabled;
}

char mouse_read_key(void) {
    unsigned char now;

    if (!mouse.enabled || !joy_guard_on)
        return cbm_k_getin();

    now = JIFFY_LO;
    if ((unsigned char)(now - joy_guard_stamp) >= JOY_KEY_GUARD)
        joy_guard_on = 0;

    /* Drop whatever SCNKEY queued from the grounded row. The count is
       cleared with IRQs off so the scan cannot refill it first. */
    __asm__ volatile ("sei" ::: "memory");
    KBD_COUNT = 0;
    __asm__ volatile ("cli" ::: "memory");
    return 0;
}

void mouse_enable(void) {
    unsigned int i;
    volatile unsigned char temp;
    
    /* Port 1 directions and fire are inputs. Bit 0 is also the 1351 right button. */
    CIA1_DDRB &= (unsigned char)~0x1F;
    joy_held = 0;
    joy_guard_on = 0;
    
    // Long delay to let POT values completely stabilize
    for (i = 0; i < 5000; i++) {
        temp = SID_POTX;
        temp = SID_POTY;
    }
    
    // Read initial POT values many times
    for (i = 0; i < 50; i++) {
        oldpotx = SID_POTX;
        oldpoty = SID_POTY;
    }
    
    // Start at center of screen
    mouse.x = 20;
    mouse.y = 12;
    old_x = mouse.x;
    old_y = mouse.y;
    mouse_visible = 0;
    
    mouse.enabled = 1;

    /* The settle loop above is long enough for SCNKEY to queue a ghost
       if the stick was already over. Drop it before the next read. */
    __asm__ volatile ("sei" ::: "memory");
    KBD_COUNT = 0;
    __asm__ volatile ("cli" ::: "memory");
}

void mouse_disable(void) {
    mouse.enabled = 0;
    joy_guard_on = 0;
    if (mouse_visible) {
        mouse_hide_cursor();
    }
}

unsigned char mouse_in_edit_area(void) {
    return (mouse.y >= 1 && mouse.y <= EDIT_HEIGHT);
}

unsigned char mouse_in_status_line(void) {
    return (mouse.y == 24);
}

void mouse_to_editor_pos(unsigned char *line, unsigned char *col) {
    if (mouse.y < 1) {
        *line = 0;
        *col = 0;
        return;
    }
    
    int screen_line = mouse.y - 1;
    *line = scroll_offset + screen_line;
    
    if (mouse.x < 3) {
        *col = 0;
    } else {
        *col = mouse.x - 3;
    }
    
    if (*line >= num_lines) *line = num_lines - 1;
    if (*col > EDIT_WIDTH) *col = EDIT_WIDTH;
}