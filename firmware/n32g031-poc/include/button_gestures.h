#ifndef RAZ_POC_BUTTON_GESTURES_H
#define RAZ_POC_BUTTON_GESTURES_H

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT,
    BUTTON_EVENT_LONG,
    BUTTON_EVENT_DOUBLE
} button_event_t;

void button_gestures_init(void);
button_event_t button_gestures_poll(void);

#endif
