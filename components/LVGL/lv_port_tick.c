#include "esp_timer.h"
#include "lvgl.h"

static void lv_tick_timer_cb(void *arg)
{
    lv_tick_inc(1); // 每 1ms 递增一次
}

void lv_port_tick_init(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = &lv_tick_timer_cb,
        .name = "lv_tick",
    };
    esp_timer_handle_t tick_timer;
    esp_timer_create(&timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 1000); // 1000 µs = 1 ms
}