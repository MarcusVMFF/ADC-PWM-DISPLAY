#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "pico/bootrom.h"  
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"

// Definições do display
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

// Definições do joystick, leds e botões
#define VRX_PIN 26  
#define VRY_PIN 27  
#define RED_LED_PIN 13  
#define BLUE_LED_PIN 12  
#define GREEN_LED_PIN 11 
#define SW_PIN 22
#define BUTTON_A 5
#define BUTTON_B 6

// Variaveis necessárias para deboucing e funcionamento do botão A
uint32_t last_time_sw = 0;  
uint32_t last_time_btn = 0; 
bool ledpwm = true;

// Definições de posições para o efeito dos LEDs e posições do quadrado
#define CENTRAL_POSITION 1970  
#define TOLERANCE 100          
#define MAX_PWM 4095           
#define MID_PWM (MAX_PWM / 2)  

// INICIAÇÃO PADRÃO PWM
uint pwm_init_gpio(uint gpio, uint wrap) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_enabled(slice_num, true);
    return slice_num;
}
// Rotina de interrupção para o pushbutton do joystick, do botão A e botão B com debouncing
void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (gpio == SW_PIN && (current_time - last_time_sw > 300000)) {
        last_time_sw = current_time;  

        bool led_state = gpio_get(GREEN_LED_PIN); 
        gpio_put(GREEN_LED_PIN, !led_state); 

        printf("LED verde alternado para %s\n", led_state ? "DESLIGADO" : "LIGADO");
    }

    if (gpio == BUTTON_A && (current_time - last_time_btn > 300000)) {
        last_time_btn = current_time;  
        ledpwm = !ledpwm;

        if (!ledpwm) {
            pwm_set_gpio_level(RED_LED_PIN, 0);  
            pwm_set_gpio_level(BLUE_LED_PIN, 0); 
            printf("LED PWM desligado\n");
        } else {
            printf("LED PWM ligado\n");
        }
    }

    if (gpio == BUTTON_B) {
        reset_usb_boot(0, 0);  
    }
}

int main() {
    stdio_init_all();
    adc_init();

    // Inicializações padrões do dispplay
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); 
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); 
    gpio_pull_up(I2C_SDA); 
    gpio_pull_up(I2C_SCL); 
    ssd1306_t ssd; 
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); 
    ssd1306_config(&ssd); 
    ssd1306_send_data(&ssd); 

    // Inicialização padrão dos pinos do joystick 
    adc_gpio_init(VRX_PIN); 
    adc_gpio_init(VRY_PIN); 

    // Inicializa o LED verde
    gpio_init(GREEN_LED_PIN);
    gpio_set_dir(GREEN_LED_PIN, GPIO_OUT);
    gpio_put(GREEN_LED_PIN, 0);

    // Inicializa todos os pushbuttons
    gpio_init(SW_PIN);
    gpio_set_dir(SW_PIN, GPIO_IN);
    gpio_pull_up(SW_PIN);
    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_pull_up(BUTTON_A);
    gpio_init(BUTTON_B); 
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);

    // Configura as interrupções para todos os botões
    gpio_set_irq_enabled(SW_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BUTTON_A, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BUTTON_B, GPIO_IRQ_EDGE_FALL, true);  
    gpio_set_irq_callback(&gpio_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);

    // Inicializações dos LEDs PWM
    uint pwm_wrap = MAX_PWM;
    pwm_init_gpio(RED_LED_PIN, pwm_wrap);
    pwm_init_gpio(BLUE_LED_PIN, pwm_wrap);
    bool cor = true;

    int thickness = 1;  // Espessura inicial da borda

    while (true) {

        // Configurações de leitura do ADC
        adc_select_input(0);
        uint16_t vrx_value = adc_read();  
        adc_select_input(1);
        uint16_t vry_value = adc_read();

        // FUnção que aumenta a espessura da borda
        if (!gpio_get(SW_PIN)) {
            thickness = 3;  
        } else {
            thickness = 1;  
        }

        // Mapeia os valores do joystick para as coordenadas do display
        int square_x = ((4095 - vrx_value) * 56) / 4095;  
        int square_y = (vry_value * 120) / 4095;   

        // Limita as coordenadas para evitar que o quadrado saia da tela
        if (square_y < 0) square_y = 0;
        if (square_y > 120) square_y = 120; 
        if (square_x < 0) square_x = 0;
        if (square_x > 56) square_x = 56;    

        ssd1306_fill(&ssd, !cor);

        // Desenha a borda do retângulo com a espessura atual
        for (int i = 0; i < thickness; i++) {
            ssd1306_rect(&ssd, 3 - i, 3 - i, 122 + 2 * i, 60 + 2 * i, cor, false);
        }

        // Desenha o quadrado na posição mapeada
        ssd1306_rect(&ssd, square_x, square_y, 8, 8, cor, true);

        ssd1306_send_data(&ssd);

        // Função dos LEDs PWM, pode ser desativada pelo botão A através da variavel "ledpwm", mapeia as coordenadas do ADC para aumentar ou diminuir a potência do LED
        if (ledpwm) {
            if (vrx_value > (CENTRAL_POSITION - TOLERANCE) && vrx_value < (CENTRAL_POSITION + TOLERANCE)) {
                pwm_set_gpio_level(RED_LED_PIN, 0);
            } else {
                int distance_from_center_red = abs(vrx_value - CENTRAL_POSITION);
                uint16_t mapped_value_red = (distance_from_center_red * MAX_PWM) / (MAX_PWM - MID_PWM);
                pwm_set_gpio_level(RED_LED_PIN, mapped_value_red);
            }

            if (vry_value > (CENTRAL_POSITION - TOLERANCE) && vry_value < (CENTRAL_POSITION + TOLERANCE)) {
                pwm_set_gpio_level(BLUE_LED_PIN, 0);
            } else {
                int distance_from_center_blue = abs(vry_value - CENTRAL_POSITION);
                uint16_t mapped_value_blue = (distance_from_center_blue * MAX_PWM) / (MAX_PWM - MID_PWM);
                pwm_set_gpio_level(BLUE_LED_PIN, mapped_value_blue);
            }
        }

        sleep_ms(100);
    }

    return 0;
}