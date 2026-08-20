#include <driver/gpio.h>
#include <esp_timer.h>
#include <fakeap.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <logic.h>

#define IDLE_TIMEOUT_US (3 * 1000 * 1000)
#define MOTOR_GPIO GPIO_NUM_2 // v i i i bratee e e e
#define BALL_BTN GPIO_NUM_21  // this is SW 520D pin

#define ON 1
#define OFF 0

enum Actions {
    WifiDeauth = 0,
    BleSpam = 1,
    FakeAP = 2,
};

static int selection_state = 0;
static int last_value = 0;

// use external anthenna
void configure_external_antenna(void) {
    // GPIO3 = RF_SWITCH_EN  LOW = switch powered on
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_3, 0);

    vTaskDelay(pdMS_TO_TICKS(100));

    // GPIO14 = RF_ANT_SELECT  HIGH = external antenna, LOW = internal
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_14, 1);
}

// REMEMBER THAT IN buzz() you commented yout actuall spining functionality
void app_main(void) {
    configure_external_antenna(); // this must be here
    // FOR LOVE OF GOD NEVER TOUCH THIS LINE ABOVE

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(MOTOR_GPIO, 0); // OFF

    gpio_config_t ball_conf = {
        .pin_bit_mask = (1ULL << BALL_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ball_conf);

    int64_t last_move_time = 0;
    bool idle_reminded = false;

    while (true) {
        int ball_value = gpio_get_level(BALL_BTN);
        printf("BALL POSITION: %d\n", ball_value);
        if (has_ball_moved(ball_value, last_value)) {
            update(&selection_state);
            last_move_time = esp_timer_get_time();
            idle_reminded = false;
        }

        int64_t now = esp_timer_get_time();
        // that means that user has "picked"
        if (!idle_reminded && (now - last_move_time) >= IDLE_TIMEOUT_US) {
            idle_reminded = true;
            switch (selection_state) {
            case WifiDeauth:
                buzz(3);
                break;

            case BleSpam:
                buzz(2);
                break;

            case FakeAP:
                buzz(1);
                ap_config_t config = {
                    .SSID = "HELLO",
                    .PASSWORD = "NOT GIVING U",
                    .MAX_CONNECTIONS = 4,
                    .WIFI_CHANNEL = 2,
                };
                create_fake_ap(config);
                break;
            }
        }

        last_value = ball_value;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void buzz(int time) {
    printf("BUZZING");
    // spin_motor(time);
    // stop_spinning(time);
}

void spin_motor(int second) {
    gpio_set_level(MOTOR_GPIO, ON);
    vTaskDelay(pdMS_TO_TICKS(1000 * second));
    return;
}

void stop_spinning(int second) {
    gpio_set_level(MOTOR_GPIO, OFF);
    vTaskDelay(pdMS_TO_TICKS(1000 * second));
    return;
}
