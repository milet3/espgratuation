/* ============================================================
 * touch_driver.h — XPT2046 触摸驱动接口
 * ============================================================ */
#ifndef __TOUCH_DRIVER_H__
#define __TOUCH_DRIVER_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void touch_driver_init(void);
    bool touch_get_xy(uint16_t *x, uint16_t *y, bool *pressed);

#ifdef __cplusplus
}
#endif

#endif /* __TOUCH_DRIVER_H__ */