#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "inttypes.h"
#include "freertos/FreeRTOS.h"
#include "esp_adc_cal.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "driver/adc.h"
#include "lcm/mbot_lcm_msgs_serial.h"
#include "lcm/comms.h"
#include "mbot_params.h"
#include "host.h"
#include "esp_log.h"
#include "wifi.h"

esp_now_peer_info_t* peers[8];
size_t curr_bot = 0;
static int peerNum = 0;

static uint32_t last_button = 0;
static uint32_t last_press = 0;
static uint32_t last_switch = 0;

static QueueHandle_t gpio_evt_queue = NULL;
static esp_adc_cal_characteristics_t adc1_chars;

static bool mode = 1;

TaskHandle_t serialMode;
TaskHandle_t controllerMode;

uint8_t* command_serializer(float vx, float vy, float wz){
    serial_twist2D_t msg = {
        .vx = vx,
        .vy = vy,
        .wz = wz
    };
    
    // Initialize variables for packet
    size_t msg_len = sizeof(msg);
    uint8_t* msg_serialized = (uint8_t*)(malloc(msg_len));
    uint8_t* packet = (uint8_t*)(malloc(msg_len + ROS_PKG_LENGTH));

    // Serialize message and create packet
    twist2D_t_serialize(&msg, msg_serialized);
    encode_msg(msg_serialized, msg_len, MBOT_VEL_CMD, packet, msg_len + ROS_PKG_LENGTH);
    free(msg_serialized);
    return packet;
}

//ISR for a button press
static void buttons_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    uint32_t ticks = xTaskGetTickCount();
    if ((gpio_num == last_button) && ((ticks - last_press) < 30)) return;
    last_button = gpio_num;
    last_press = ticks;
    //xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
    // esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    // if (peer == NULL) return;
    // ESP_ERROR_CHECK( esp_now_fetch_peer(false, peer));// != ESP_OK) return;
    if (gpio_num == 9) curr_bot = (curr_bot + 1) % peerNum;
    else curr_bot = (curr_bot == 0)? peerNum - 1: curr_bot - 1;
    memcpy(send_param->dest_mac, peers[curr_bot]->peer_addr, ESP_NOW_ETH_ALEN);
    //free(peer);
}

//ISR for switch (change modes)
static void switch_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    uint32_t ticks = xTaskGetTickCount();
    if ((ticks - last_switch) < 100) return;
    last_switch = ticks;
    if(mode) {
        gpio_isr_handler_add(B1_PIN, buttons_isr_handler, (void*) B1_PIN);
        gpio_isr_handler_add(B2_PIN, buttons_isr_handler, (void*) B2_PIN);
        vTaskResume(controllerMode);
        vTaskSuspend(serialMode);
    }
    else{
        gpio_isr_handler_remove(B1_PIN);
        gpio_isr_handler_remove(B2_PIN);
        vTaskSuspend(controllerMode);
        vTaskResume(serialMode);
    }
    mode = !mode;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

void controller_init(){
    //configure the ADC
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);

    //check for failures
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc1_config_channel_atten(JS_Y_PIN, ADC_ATTEN_DB_11));

    gpio_config_t GPIO = {};
     //interrupt of rising edge (release button)
    GPIO.intr_type = GPIO_INTR_POSEDGE;
    //bit mask of the pins, use GPIO4/5 here
    GPIO.pin_bit_mask = (0b1 << B1_PIN) | (0b1 << B2_PIN);
    //set as input mode
    GPIO.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    GPIO.pull_up_en = 1;
    gpio_config(&GPIO);
    //configure switch interrupt
    GPIO.intr_type = GPIO_INTR_ANYEDGE;
    //bit mask of the pins, use GPIO4/5 here
    GPIO.pin_bit_mask = (0b1 << SW_PIN);
    //set as input mode
    GPIO.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    GPIO.pull_down_en = 1;
    gpio_config(&GPIO);

    //install gpio isr service
    gpio_install_isr_service(0);

    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(B1_PIN, buttons_isr_handler, (void*) B1_PIN);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(B2_PIN, buttons_isr_handler, (void*) B2_PIN);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(SW_PIN, switch_isr_handler, (void*) SW_PIN);
}

#endif