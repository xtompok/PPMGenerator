#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/usart.h>
#include <stdio.h>
#include <errno.h>

#define NUM_CHANNELS 8

#define PPM_PORT    GPIOA
#define PPM_PIN     GPIO8
#define PPM_TIMER   TIM2

#define LED_PORT    GPIOC
#define LED_PIN     GPIO13

#define SYNCHRO_WIDTH   300
#define TOTAL_WIDTH     22500

#define STATE_WAS_SIGNAL    1
#define STATE_WAS_SYNCHRO   2

uint16_t channels[NUM_CHANNELS];
volatile uint8_t channel;
volatile uint8_t state;
volatile uint16_t last_oc_value;

volatile uint32_t __millis;

static void clock_setup(void){
    rcc_clock_setup_in_hse_8mhz_out_72mhz();
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_TIM2);
    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_USART1);
}

static void gpio_setup(void){

    gpio_set_mode(LED_PORT,GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, LED_PIN);
    gpio_set_mode(PPM_PORT,GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, PPM_PIN);
    gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON,AFIO_MAPR_USART1_REMAP);
    gpio_set_mode(GPIO_BANK_USART1_RE_TX, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART1_RE_TX);
    gpio_set_mode(GPIO_BANK_USART1_RE_RX, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART1_RE_RX);
}

static void tim_setup(void){
    nvic_enable_irq(NVIC_TIM2_IRQ);
    timer_set_mode(PPM_TIMER,TIM_CR1_CKD_CK_INT,TIM_CR1_CMS_EDGE,TIM_CR1_DIR_UP);
    timer_set_prescaler(PPM_TIMER, 72);

    timer_disable_preload(PPM_TIMER);
    timer_continuous_mode(PPM_TIMER);

    timer_set_period(PPM_TIMER,TOTAL_WIDTH);
    timer_set_oc_value(PPM_TIMER,TIM_OC1,SYNCHRO_WIDTH);

    timer_enable_counter(PPM_TIMER);
    timer_enable_irq(PPM_TIMER, TIM_DIER_CC1IE);
    timer_enable_irq(PPM_TIMER, TIM_DIER_UIE);
}

static void usart_setup(void){
    usart_set_baudrate(USART1, 115200);
    usart_set_databits(USART1, 8);
    usart_set_stopbits(USART1, USART_STOPBITS_1);
    usart_set_parity(USART1, USART_PARITY_NONE);
    usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);
    usart_set_mode(USART1, USART_MODE_TX_RX);

    usart_enable(USART1);
}

static void systick_setup(void){
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
    systick_set_reload(8999);
    systick_interrupt_enable();
    systick_counter_enable();
}

void sys_tick_handler(void){
    __millis++;
}

void delay(uint32_t ms){
    uint32_t now;
    now = __millis;
    while (__millis < now+ms){}
}

void tim2_isr(void){
    if (timer_get_flag(PPM_TIMER,TIM_SR_CC1IF)){        
        timer_clear_flag(PPM_TIMER,TIM_SR_CC1IF);
        if (state == STATE_WAS_SIGNAL){
            gpio_clear(PPM_PORT,PPM_PIN);

            channel++;
            channel %= NUM_CHANNELS+1;
            state = STATE_WAS_SYNCHRO;

            last_oc_value += SYNCHRO_WIDTH;
            timer_set_oc_value(PPM_TIMER,TIM_OC1,last_oc_value);
        } else if (state == STATE_WAS_SYNCHRO){
            gpio_set(PPM_PORT,PPM_PIN);

            if (channel == NUM_CHANNELS){ // Already at last channel, wait for next iteration
                return;
            }

            state = STATE_WAS_SIGNAL;

            last_oc_value += channels[channel];
            timer_set_oc_value(PPM_TIMER,TIM_OC1,last_oc_value);            
        }


    } else if (timer_get_flag(PPM_TIMER,TIM_SR_UIF)){
        timer_clear_flag(PPM_TIMER,TIM_SR_UIF);
        channel = 0;
        last_oc_value = SYNCHRO_WIDTH;
        state = STATE_WAS_SYNCHRO;
        gpio_clear(PPM_PORT,PPM_PIN);
        timer_set_oc_value(PPM_TIMER, TIM_OC1, last_oc_value);
        gpio_toggle(LED_PORT,LED_PIN);

    }
}

int _write(int file, char *ptr, int len)
{
	int i;

	if (file == 1) {
		for (i = 0; i < len; i++)
			usart_send_blocking(USART1, ptr[i]);
		return i;
	}

	errno = EIO;
	return -1;
}


int main(){
    clock_setup();
    gpio_setup();
    usart_setup();
    systick_setup();

    for (int i=0;i<NUM_CHANNELS;i++){
        channels[i] = 1000;
    }

    tim_setup();

    printf("USART initialized");

    while(1){
        printf("Channels: ");
        for (int i=0; i<NUM_CHANNELS; i++){
            printf("%d:%d, ",i,channels[i]);
        }
        printf("\n");
        delay(100);
    }
}