// widget.c
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/peripheral.h>

#if __has_include(<zmk/split/central.h>)
#include <zmk/split/central.h>
#else
#include <zmk/split/bluetooth/central.h>
#endif

#include <zephyr/logging/log.h>

#include <zmk_rgbled_widget/widget.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// PWM设备定义
static const struct pwm_dt_spec pwm_red = PWM_DT_SPEC_GET(DT_ALIAS(pwm_red));
static const struct pwm_dt_spec pwm_green = PWM_DT_SPEC_GET(DT_ALIAS(pwm_green));
static const struct pwm_dt_spec pwm_blue = PWM_DT_SPEC_GET(DT_ALIAS(pwm_blue));

// PWM周期（纳秒），对应约1kHz频率
#define PWM_PERIOD_NS 1000000

// 设备树节点检查
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(pwm_red)),
             "An alias for red PWM LED is not found for RGBLED_WIDGET");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(pwm_green)),
             "An alias for green PWM LED is not found for RGBLED_WIDGET");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(pwm_blue)),
             "An alias for blue PWM LED is not found for RGBLED_WIDGET");

// 颜色定义（在.c文件中定义静态常量）
static const struct pwm_color COLOR_BLACK = {0, 0, 0};
static const struct pwm_color COLOR_RED = {0xFFFF, 0, 0};
static const struct pwm_color COLOR_GREEN = {0, 0xFFFF, 0};
static const struct pwm_color COLOR_BLUE = {0, 0, 0xFFFF};
static const struct pwm_color COLOR_YELLOW = {0xFFFF, 0xFFFF, 0};
static const struct pwm_color COLOR_MAGENTA = {0xFFFF, 0, 0xFFFF};
static const struct pwm_color COLOR_CYAN = {0, 0xFFFF, 0xFFFF};
static const struct pwm_color COLOR_WHITE = {0xFFFF, 0xFFFF, 0xFFFF};
static const struct pwm_color COLOR_DARK_RED = {0x8000, 0, 0};
static const struct pwm_color COLOR_DARK_GREEN = {0, 0x8000, 0};
static const struct pwm_color COLOR_DARK_BLUE = {0, 0, 0x8000};
static const struct pwm_color COLOR_ORANGE = {0xFFFF, 0x8000, 0};


// 扩展的颜色映射（支持更多颜色）
static const struct pwm_color color_map[] = {
    COLOR_BLACK,      // 索引 0: 黑色
    COLOR_RED,        // 索引 1: 红色
    COLOR_GREEN,      // 索引 2: 绿色
    COLOR_YELLOW,     // 索引 3: 黄色
    COLOR_BLUE,       // 索引 4: 蓝色
    COLOR_MAGENTA,    // 索引 5: 洋红
    COLOR_CYAN,       // 索引 6: 青色
    COLOR_WHITE,      // 索引 7: 白色
    COLOR_DARK_RED,   // 索引 8: 暗红
    COLOR_DARK_GREEN, // 索引 9: 暗绿
    COLOR_DARK_BLUE,  // 索引 10: 暗蓝
    COLOR_ORANGE,     // 索引 11: 橙色
};

// 将颜色索引转换为PWM颜色
struct pwm_color index_to_pwm_color(uint8_t index) {
    if (index < ARRAY_SIZE(color_map)) {
        return color_map[index];
    }
    return COLOR_BLACK; // 默认黑色
}

// PWM设备状态检查函数
static bool check_pwm_devices(void) {
    if (!device_is_ready(pwm_red.dev)) {
        LOG_ERR("Red PWM device is not ready");
        return false;
    }
    if (!device_is_ready(pwm_green.dev)) {
        LOG_ERR("Green PWM device is not ready");
        return false;
    }
    if (!device_is_ready(pwm_blue.dev)) {
        LOG_ERR("Blue PWM device is not ready");
        return false;
    }
    return true;
}

// 设置PWM颜色
void set_pwm_color(struct pwm_color color) {
    int ret;
    
    // 检查设备是否就绪
    if (!device_is_ready(pwm_red.dev) || 
        !device_is_ready(pwm_green.dev) || 
        !device_is_ready(pwm_blue.dev)) {
        LOG_ERR("PWM devices not ready");
        return;
    }
    
    // 设置红色通道
    ret = pwm_set_pulse_dt(&pwm_red, color.r);
    if (ret < 0) {
        LOG_ERR("Failed to set red PWM: %d", ret);
    }
    
    // 设置绿色通道
    ret = pwm_set_pulse_dt(&pwm_green, color.g);
    if (ret < 0) {
        LOG_ERR("Failed to set green PWM: %d", ret);
    }
    
    // 设置蓝色通道
    ret = pwm_set_pulse_dt(&pwm_blue, color.b);
    if (ret < 0) {
        LOG_ERR("Failed to set blue PWM: %d", ret);
    }
}

// 修改set_rgb_leds函数为PWM版本
static void set_rgb_leds(struct pwm_color color, uint16_t duration_ms) {
    set_pwm_color(color);
    
    if (duration_ms > 0) {
        k_sleep(K_MSEC(duration_ms));
    }
}

// 修改blink_item结构体以支持PWM颜色
struct blink_item {
    struct pwm_color color;
    uint16_t duration_ms;
    uint16_t sleep_ms;
};

// 当前颜色状态
static struct pwm_color current_color = COLOR_BLACK;
static struct pwm_color persistent_color = COLOR_BLACK;

// 消息队列
K_MSGQ_DEFINE(led_msgq, sizeof(struct blink_item), 16, 1);

// 修改消息处理线程
extern void led_process_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    k_work_init_delayable(&indicate_connectivity_work, indicate_connectivity_cb);

#if SHOW_LAYER_CHANGE
    k_work_init_delayable(&layer_indicate_work, indicate_layer_cb);
#endif

    while (true) {
        struct blink_item blink;
        k_msgq_get(&led_msgq, &blink, K_FOREVER);
        
        if (blink.duration_ms > 0) {
            LOG_DBG("PWM blink: R:%04X G:%04X B:%04X, duration %d", 
                   blink.color.r, blink.color.g, blink.color.b, blink.duration_ms);

            set_rgb_leds(blink.color, blink.duration_ms);
            
            // 恢复持久颜色
            if (blink.sleep_ms > 0) {
                set_rgb_leds(persistent_color, blink.sleep_ms);
            } else {
                set_rgb_leds(persistent_color, CONFIG_RGBLED_WIDGET_INTERVAL_MS);
            }
        } else {
            LOG_DBG("PWM persistent color: R:%04X G:%04X B:%04X", 
                   blink.color.r, blink.color.g, blink.color.b);
            persistent_color = blink.color;
            set_rgb_leds(blink.color, 0);
        }
    }
}

// 修改其他函数以使用新的颜色系统...

// 修改indicate_connectivity_internal函数
static void indicate_connectivity_internal(void) {
    struct blink_item blink = {.duration_ms = CONFIG_RGBLED_WIDGET_CONN_BLINK_MS};

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    switch (zmk_endpoints_selected().transport) {
    case ZMK_TRANSPORT_USB:
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_CONN_SHOW_USB)
        blink.color = index_to_pwm_color(CONFIG_RGBLED_WIDGET_CONN_COLOR_USB);
        break;
#endif
    default:
#if IS_ENABLED(CONFIG_ZMK_BLE)
        uint8_t profile_index = zmk_ble_active_profile_index();
        if (zmk_ble_active_profile_is_connected()) {
            blink.color = index_to_pwm_color(CONFIG_RGBLED_WIDGET_CONN_COLOR_CONNECTED);
        } else if (zmk_ble_active_profile_is_open()) {
            blink.color = index_to_pwm_color(CONFIG_RGBLED_WIDGET_CONN_COLOR_ADVERTISING);
        } else {
            blink.color = index_to_pwm_color(CONFIG_RGBLED_WIDGET_CONN_COLOR_DISCONNECTED);
        }
#endif
        break;
    }
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    if (zmk_split_bt_peripheral_is_connected()) {
        blink.color = index_to_pwm_color(CONFIG_RGBLED_WIDGET_CONN_COLOR_CONNECTED);
    } else {
        blink.color = index_to_pwm_color(CONFIG_RGBLED_WIDGET_CONN_COLOR_DISCONNECTED);
    }
#endif

    k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
}

// 修改电池指示相关函数
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
static inline struct pwm_color get_battery_pwm_color(uint8_t battery_level) {
    if (battery_level == 0) {
        return index_to_pwm_color(CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MISSING);
    }
    if (battery_level >= CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_HIGH) {
        return index_to_pwm_color(CONFIG_RGBLED_WIDGET_BATTERY_COLOR_HIGH);
    }
    if (battery_level >= CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_LOW) {
        return index_to_pwm_color(CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MEDIUM);
    }
    return index_to_pwm_color(CONFIG_RGBLED_WIDGET_BATTERY_COLOR_LOW);
}

void indicate_battery(void) {
    struct blink_item blink = {.duration_ms = CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS};

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BATTERY_SHOW_SELF)
    uint8_t battery_level = zmk_battery_state_of_charge();
    blink.color = get_battery_pwm_color(battery_level);
    k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
#endif
}
#endif

// 修改层颜色指示
#if SHOW_LAYER_COLORS
void update_layer_color(void) {
    uint8_t index = zmk_keymap_highest_layer_active();
    struct pwm_color new_color = index_to_pwm_color(layer_color_idx[index]);

    if (new_color.r != persistent_color.r || new_color.g != persistent_color.g || new_color.b != persistent_color.b) {
        struct blink_item color_item = {.color = new_color};
        k_msgq_put(&led_msgq, &color_item, K_NO_WAIT);
    }
}
#endif

// 修改层变化指示
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
void indicate_layer(void) {
    uint8_t index = zmk_keymap_highest_layer_active();
    struct pwm_color blink_color = index_to_pwm_color(CONFIG_RGBLED_WIDGET_LAYER_COLOR);
    
    for (int i = 0; i < index; i++) {
        struct blink_item blink = {
            .duration_ms = CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS,
            .color = blink_color,
            .sleep_ms = CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS
        };
        k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
    }
}
#endif

// 其他函数保持不变...
// 其他函数保持不变...

#if SHOW_LAYER_CHANGE
static struct k_work_delayable layer_indicate_work;

static int led_layer_listener_cb(const zmk_event_t *eh) {
    // ignore if not initialized yet or layer off events
    if (initialized && as_zmk_layer_state_changed(eh)->state) {
        k_work_reschedule(&layer_indicate_work, K_MSEC(CONFIG_RGBLED_WIDGET_LAYER_DEBOUNCE_MS));
    }
    return 0;
}

static void indicate_layer_cb(struct k_work *work) { indicate_layer(); }

ZMK_LISTENER(led_layer_listener, led_layer_listener_cb);
ZMK_SUBSCRIPTION(led_layer_listener, zmk_layer_state_changed);
#endif // SHOW_LAYER_CHANGE

extern void led_process_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    k_work_init_delayable(&indicate_connectivity_work, indicate_connectivity_cb);

#if SHOW_LAYER_CHANGE
    k_work_init_delayable(&layer_indicate_work, indicate_layer_cb);
#endif

    while (true) {
        // wait until a blink item is received and process it
        struct blink_item blink;
        k_msgq_get(&led_msgq, &blink, K_FOREVER);
        if (blink.duration_ms > 0) {
            LOG_DBG("Got a blink item from msgq, color %d, duration %d", blink.color,
                    blink.duration_ms);

            // Blink the leds, using a separation blink if necessary
            if (blink.color == led_current_color && blink.color > 0) {
                set_rgb_leds(0, CONFIG_RGBLED_WIDGET_INTERVAL_MS);
            }
            set_rgb_leds(blink.color, blink.duration_ms);
            if (blink.color == led_layer_color && blink.color > 0) {
                set_rgb_leds(0, CONFIG_RGBLED_WIDGET_INTERVAL_MS);
            }
            // wait interval before processing another blink
            set_rgb_leds(led_layer_color,
                         blink.sleep_ms > 0 ? blink.sleep_ms : CONFIG_RGBLED_WIDGET_INTERVAL_MS);

        } else {
            LOG_DBG("Got a layer color item from msgq, color %d", blink.color);
            set_rgb_leds(blink.color, 0);
        }
    }
}

// define led_process_thread with stack size 1024, start running it 100 ms after
// boot
K_THREAD_DEFINE(led_process_tid, 1024, led_process_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
/*
extern void led_init_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    // check and indicate battery level on thread start
    LOG_INF("Indicating initial battery status");

    indicate_battery();

    // wait until blink should be displayed for further checks
    k_sleep(K_MSEC(CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS + CONFIG_RGBLED_WIDGET_INTERVAL_MS));
#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

    // check and indicate current profile or peripheral connectivity status
    LOG_INF("Indicating initial connectivity status");
    indicate_connectivity();

#if SHOW_LAYER_COLORS
    LOG_INF("Setting initial layer color");
    update_layer_color();
#endif // SHOW_LAYER_COLORS

    initialized = true;
    LOG_INF("Finished initializing LED widget");
}
*/
// 修改初始化线程，添加PWM设备检查
extern void led_init_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    // 检查PWM设备是否就绪
    if (!check_pwm_devices()) {
        LOG_ERR("PWM devices not available, RGB LED widget disabled");
        return;
    }

    LOG_INF("PWM RGB LED widget initialized successfully");

    k_work_init_delayable(&indicate_connectivity_work, indicate_connectivity_cb);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    // 检查并指示电池电平
    LOG_INF("Indicating initial battery status");
    indicate_battery();

    k_sleep(K_MSEC(CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS + CONFIG_RGBLED_WIDGET_INTERVAL_MS));
#endif

    // 检查并指示连接状态
    LOG_INF("Indicating initial connectivity status");
    indicate_connectivity();

#if SHOW_LAYER_COLORS
    LOG_INF("Setting initial layer color");
    update_layer_color();
#endif

    initialized = true;
    LOG_INF("Finished initializing PWM RGB LED widget");
}




// run init thread on boot for initial battery+output checks
K_THREAD_DEFINE(led_init_tid, 1024, led_init_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 200);
