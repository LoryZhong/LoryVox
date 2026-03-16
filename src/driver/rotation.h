#ifndef _ROTATION_H_
#define _ROTATION_H_

// Angle precision: using 30 bits to represent a full rotation.
// Integer representation avoids floating point
#define ROTATION_PRECISION 30
#define ROTATION_FULL (1<<ROTATION_PRECISION)
#define ROTATION_HALF (1<<(ROTATION_PRECISION-1))
#define ROTATION_MASK ((1<<ROTATION_PRECISION)-1)

//external variables used by rotation.c
extern uint32_t rotation_zero;  // the angle corresponding to the zero position of the display, 
extern bool rotation_stopped;   //Stopped rotation or not
extern uint32_t rotation_period_raw; // the latest measured rotation period based on the sync signal. (us)
extern uint32_t rotation_period;     // the smoothed rotation period calculated from the history. (us)
extern bool rotation_lock;   // Enable or disable rotation speed lock.
extern int32_t rotation_drift;  // the correction to apply to the rotation speed to compensate for drift.
void rotation_init(void);
uint32_t rotation_current_angle(void);


#endif
