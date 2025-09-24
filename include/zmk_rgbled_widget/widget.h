// widget.h
#define SHOW_LAYER_CHANGE                                                                          \
    (IS_ENABLED(CONFIG_RGBLED_WIDGET_SHOW_LAYER_CHANGE)) &&                                        \
        (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#define SHOW_LAYER_COLORS                                                                          \
    (IS_ENABLED(CONFIG_RGBLED_WIDGET_SHOW_LAYER_COLORS)) &&                                        \
        (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

// PWM颜色结构
struct pwm_color {
    uint16_t r;
    uint16_t g;
    uint16_t b;
};

// 预定义颜色（16位PWM值）
#define PWM_COLOR_BLACK    {0, 0, 0}
#define PWM_COLOR_RED      {0xFFFF, 0, 0}
#define PWM_COLOR_GREEN    {0, 0xFFFF, 0}
#define PWM_COLOR_BLUE     {0, 0, 0xFFFF}
#define PWM_COLOR_YELLOW   {0xFFFF, 0xFFFF, 0}
#define PWM_COLOR_MAGENTA  {0xFFFF, 0, 0xFFFF}
#define PWM_COLOR_CYAN     {0, 0xFFFF, 0xFFFF}
#define PWM_COLOR_WHITE    {0xFFFF, 0xFFFF, 0xFFFF}

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
void indicate_battery(void);
#endif

#if IS_ENABLED(CONFIG_ZMK_BLE)
void indicate_connectivity(void);
#endif

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
void indicate_layer(void);
#endif

// PWM相关函数
void set_pwm_color(struct pwm_color color);
struct pwm_color index_to_pwm_color(uint8_t index);
