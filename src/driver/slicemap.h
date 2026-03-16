#ifndef _SLICEMAP_H_
#define _SLICEMAP_H_

#include "voxel.h"

//slicing a circle into 360 clips (one per degree)
#define SLICE_COUNT 360
#define SLICE_QUADRANT (SLICE_COUNT / 4)
#define SLICE_WRAP(slice) ((slice) % (SLICE_COUNT))
//slice_index use： uint8_t if 256 slices fit, else uint16_t
#if SLICE_COUNT <= 256
    typedef uint8_t slice_index_t;
#else
    typedef uint16_t slice_index_t;
#endif

// polar coordinate of one LED on the panel
typedef struct {
    slice_index_t slice;  //slice angle in [0, 259]
    uint8_t column;     //column index for panel
} slice_polar_t;

// 2D voxel coordinate X/Y
typedef struct {
    voxel_index_t x, y;
} voxel_2D_t;

//lighting modes
typedef enum {
    SLICE_BRIGHTNESS_UNIFORM,
    SLICE_BRIGHTNESS_BOOSTED,
    SLICE_BRIGHTNESS_UNLIMITED
} slice_brightness_t;


extern voxel_2D_t slice_map[SLICE_COUNT][PANEL_WIDTH][PANEL_COUNT];
extern float eccentricity[2];

//build slice_map on startup:called once before the main loop 
void slicemap_ebr(int* a, int n);
void slicemap_init(slice_brightness_t brightness);


#endif
