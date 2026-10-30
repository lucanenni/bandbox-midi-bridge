#ifndef LVGL_PORT_H
#define LVGL_PORT_H



#ifdef __cplusplus
extern "C" {
#endif


void lvgl_port_init(void);
bool example_lvgl_lock(int timeout_ms);  // lvgl mutex, -1 = wait forever
void example_lvgl_unlock(void);


#ifdef __cplusplus
}
#endif



#endif










