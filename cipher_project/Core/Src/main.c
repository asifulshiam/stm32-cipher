/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

#include "adc.h"
#include "gpio.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fonts.h"
#include "rc522.h"
#include "ssd1306.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
typedef enum {
  STAGE_POT,
  STAGE_ULTRASONIC,
  STAGE_BUTTON_HOLD,
  STAGE_DHT_KEYPAD,
  STAGE_KEYPAD_PASSCODE
} StageType;

typedef struct {
  StageType type;
  char question[32];
  int answer;
  int tolerance;
  int hold_time_ms;
} Stage;

typedef struct {
  char question[32];
  int answer;
} QA;

QA pot_q[5] = {
    {"d/dx(3x2) at x=2", 12}, {"d/dx(5x3) at x=1", 15},
    {"d/dx(4x2) at x=3", 24}, {"d/dx(x4) at x=2", 32},
    {"d/dx(2x2+3x) x=3", 15},
};

QA alg_q[5] = {
    {"2x + 5 = 17", 6}, {"3x - 4 = 11", 5},   {"x/2 + 3 = 9", 12},
    {"4x + 1 = 25", 6}, {"5x - 10 = 40", 10},
};

int ultra_targets[4] = {10, 15, 20, 25};
int hold_times[4] = {3000, 4000, 5000, 6000};

Stage stages[6] = {
    {STAGE_POT, "", 0, 2, 0},
    {STAGE_POT, "", 0, 2, 0},
    {STAGE_ULTRASONIC, "", 0, 2, 0},
    {STAGE_BUTTON_HOLD, "HOLD BTN 4s", 0, 0, 4000},
    {STAGE_DHT_KEYPAD, "ROUND TO NEAREST 5", 0, 0, 0},
    {STAGE_KEYPAD_PASSCODE, "SET VAULT PASSCODE", 0, 0, 0},
};

volatile GameState game_state = STATE_SLEEP;
volatile uint32_t countdown_ms = 90000;
volatile uint8_t oled_flash_timer = 0;

int current_stage = 0;
int lives = 3;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void DWT_Init(void);
void delay_us(uint32_t us);
void oled_clear(void);
void oled_print(uint8_t x, uint8_t y, const char* text);
void oled_show_2lines(const char* line1, const char* line2);
void oled_show_timer(void);
uint8_t read_pot(void);
uint8_t btn_pressed(void);
uint8_t ir_detected(void);
char scan_keypad(void);
uint8_t dht11_read(uint8_t* temp, uint8_t* humidity);
uint32_t hcsr04_read_cm(void);
void buzzer_tone(uint16_t freq_hz, uint32_t duration_ms);
void buzzer_stop(void);
void update_buzzer_tick(void);
void run_stage(void);
void handle_pass(void);
void handle_fail(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void DWT_Init(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void delay_us(uint32_t us) {
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * (SystemCoreClock / 1000000);
  while ((DWT->CYCCNT - start) < ticks);
}

void oled_clear(void) {
  SSD1306_Fill(SSD1306_COLOR_BLACK);
  SSD1306_UpdateScreen();
}

void oled_print(uint8_t x, uint8_t y, const char* text) {
  SSD1306_GotoXY(x, y);
  SSD1306_Puts((char*)text, &Font_7x10, 1);
  SSD1306_UpdateScreen();
}

void oled_show_2lines(const char* line1, const char* line2) {
  SSD1306_Fill(SSD1306_COLOR_BLACK);
  SSD1306_GotoXY(0, 10);
  SSD1306_Puts((char*)line1, &Font_7x10, 1);
  SSD1306_GotoXY(0, 30);
  SSD1306_Puts((char*)line2, &Font_7x10, 1);
  SSD1306_UpdateScreen();
}

void oled_show_timer(void) {
  char buf[16];
  uint32_t secs = countdown_ms / 1000;
  snprintf(buf, sizeof(buf), "TIME: %02lus", secs);
  SSD1306_GotoXY(0, 48);
  if (oled_flash_timer && (HAL_GetTick() % 500 < 250))
    SSD1306_Puts(buf, &Font_7x10, 0);
  else
    SSD1306_Puts(buf, &Font_7x10, 1);
}

uint8_t read_pot(void) {
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 10);
  uint16_t adc = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  return (uint8_t)((adc * 99) / 4095);
}

uint8_t btn_pressed(void) {
  return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET;
}

uint8_t ir_detected(void) {
  return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15) == GPIO_PIN_RESET;
}

volatile char last_key = 0;
volatile uint32_t key_tick = 0;

char scan_keypad(void) {
  if (last_key != 0) {
    char k = last_key;
    last_key = 0;
    return k;
  }
  return 0;
}

uint8_t dht11_read(uint8_t* temp, uint8_t* humidity) {
  uint8_t data[5] = {0};
  GPIO_InitTypeDef g = {0};

  g.Pin = GPIO_PIN_0;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &g);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
  HAL_Delay(18);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
  delay_us(30);

  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &g);

  uint32_t t = HAL_GetTick();
  while (!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0))
    if (HAL_GetTick() - t > 2) return 0;
  while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0))
    if (HAL_GetTick() - t > 2) return 0;

  for (int i = 0; i < 40; i++) {
    t = HAL_GetTick();
    while (!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0))
      if (HAL_GetTick() - t > 2) return 0;
    delay_us(40);
    data[i / 8] <<= 1;
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0)) data[i / 8] |= 1;
    t = HAL_GetTick();
    while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0))
      if (HAL_GetTick() - t > 2) return 0;
  }

  if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) return 0;
  *humidity = data[0];
  *temp = data[2];
  return 1;
}

uint32_t hcsr04_read_cm(void) {
  uint32_t start, elapsed;

  // Trigger pulse (10us)
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
  delay_us(2);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
  delay_us(10);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);

  // Wait for echo HIGH (timeout 30ms)
  uint32_t t = HAL_GetTick();
  while (!HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8)) {
    if (HAL_GetTick() - t > 30) return 999;
  }
  start = DWT->CYCCNT;

  // Wait for echo LOW (timeout 100ms)
  t = HAL_GetTick();
  while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8)) {
    if (HAL_GetTick() - t > 100) return 999;
  }
  elapsed = DWT->CYCCNT - start;

  // Convert cycles to cm (72MHz clock)
  uint32_t us = elapsed / 72;
  uint32_t cm = us / 58;
  return cm;
}

void buzzer_tone(uint16_t freq_hz, uint32_t duration_ms) {
  if (freq_hz == 0) {
    HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_4);
    return;
  }
  uint16_t arr = (uint16_t)((72000000UL / (72UL * freq_hz)) - 1);
  TIM4->ARR = arr;
  TIM4->CCR4 = arr / 2;
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);
  if (duration_ms > 0) {
    HAL_Delay(duration_ms);
    HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_4);
  }
}

void buzzer_stop(void) { HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_4); }

void update_buzzer_tick(void) {
  static uint8_t tick_state = 0;
  static uint32_t tick_counter = 0;
  uint32_t tick_interval;

  if (countdown_ms > 30000)
    tick_interval = 10;
  else if (countdown_ms > 15000)
    tick_interval = 5;
  else
    tick_interval = 2;

  tick_counter++;
  if (tick_counter >= tick_interval) {
    tick_counter = 0;
    tick_state = !tick_state;
    if (tick_state) {
      TIM4->ARR = (uint16_t)((72000000UL / (72UL * 1000UL)) - 1);
      TIM4->CCR4 = TIM4->ARR / 2;
      HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);
    } else {
      HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_4);
    }
  }
}

void handle_pass(void) {
  buzzer_stop();
  oled_show_2lines("  STAGE PASSED!", "");
  buzzer_tone(1500, 100);
  HAL_Delay(50);
  buzzer_tone(2000, 150);
  HAL_Delay(800);
  current_stage++;
}

void handle_fail(void) {
  buzzer_stop();
  lives--;
  char buf[16];
  snprintf(buf, sizeof(buf), "LIVES LEFT: %d", lives);
  oled_show_2lines("  WRONG!", buf);
  buzzer_tone(200, 500);
  HAL_Delay(1000);
  if (lives <= 0) game_state = STATE_DENIED;
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM4_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  int pi, ai, ui;
  DWT_Init();

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
  SSD1306_Init();
  RC522_Init();

  HAL_Delay(10);
  uint32_t seed = HAL_GetTick();
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 10);
  seed ^= HAL_ADC_GetValue(&hadc1) << 3;
  HAL_ADC_Stop(&hadc1);
  srand(seed);

  // burn a few rand calls to decorrelate
  rand();
  rand();
  rand();

  // Randomize questions
  pi = rand() % 5;
  ai = rand() % 5;
  ui = rand() % 4;
  strncpy(stages[0].question, pot_q[pi].question, 32);
  stages[0].answer = pot_q[pi].answer;
  strncpy(stages[1].question, alg_q[ai].question, 32);
  stages[1].answer = alg_q[ai].answer;
  char ultra_buf[20];
  snprintf(ultra_buf, sizeof(ultra_buf), "TARGET: %d cm", ultra_targets[ui]);
  strncpy(stages[2].question, ultra_buf, 32);
  stages[2].answer = ultra_targets[ui];

  oled_show_2lines("  CIPHER v1.0", "WAVE TO WAKE");
  while (ir_detected() == 0);
  buzzer_tone(1000, 200);
  HAL_Delay(300);

  oled_show_2lines("PRESENT CARD", "  TO UNLOCK");
  while (RC522_Detect() == 0);
  buzzer_tone(1200, 150);
  HAL_Delay(100);
  buzzer_tone(1500, 150);
  HAL_Delay(200);

  oled_show_2lines("UNAUTHORIZED", "ACCESS DETECTED");
  buzzer_tone(400, 150);
  HAL_Delay(50);
  buzzer_tone(300, 150);
  HAL_Delay(50);
  buzzer_tone(200, 300);
  HAL_Delay(1000);

  char code_buf[20];
  snprintf(code_buf, sizeof(code_buf), "CODE: %04d", rand() % 10000);
  oled_show_2lines("SECRET CODE", code_buf);
  HAL_Delay(2000);

  oled_show_2lines("HACK INITIATED", "    90s");
  HAL_Delay(1000);

  game_state = STATE_PLAYING;
  current_stage = 0;
  lives = 3;
  countdown_ms = 90000;
  oled_flash_timer = 0;

  HAL_TIM_Base_Start_IT(&htim2);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    if (game_state == STATE_PLAYING) {
      if (current_stage >= 6) {
        game_state = STATE_WIN;
        continue;
      }
      run_stage();
    }

    else if (game_state == STATE_WIN) {
      HAL_TIM_Base_Stop_IT(&htim2);
      buzzer_stop();
      char win_buf[20];
      snprintf(win_buf, sizeof(win_buf), "TIME LEFT: %02lus",
               countdown_ms / 1000);
      oled_show_2lines("SYSTEM UNLOCKED!", win_buf);
      buzzer_tone(1000, 100);
      HAL_Delay(100);
      buzzer_tone(1500, 100);
      HAL_Delay(100);
      buzzer_tone(2000, 100);
      HAL_Delay(100);
      buzzer_tone(2500, 300);
      HAL_Delay(5000);
      goto reset_game;
    }

    else if (game_state == STATE_DENIED) {
      HAL_TIM_Base_Stop_IT(&htim2);
      buzzer_stop();
      oled_show_2lines("ACCESS DENIED", "");
      buzzer_tone(200, 1000);
      HAL_Delay(5000);
      goto reset_game;
    }

    continue;

  reset_game:
    game_state = STATE_PLAYING;
    current_stage = 0;
    lives = 3;
    countdown_ms = 90000;
    oled_flash_timer = 0;
    // Re-randomize questions
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    srand(HAL_GetTick() ^ (HAL_ADC_GetValue(&hadc1) << 3));
    HAL_ADC_Stop(&hadc1);
    pi = rand() % 5;
    ai = rand() % 5;
    ui = rand() % 4;
    strncpy(stages[0].question, pot_q[pi].question, 32);
    stages[0].answer = pot_q[pi].answer;
    strncpy(stages[1].question, alg_q[ai].question, 32);
    stages[1].answer = alg_q[ai].answer;
    char ultra_buf2[20];
    snprintf(ultra_buf2, sizeof(ultra_buf2), "TARGET: %d cm",
             ultra_targets[ui]);
    strncpy(stages[2].question, ultra_buf2, 32);
    stages[2].answer = ultra_targets[ui];
    oled_show_2lines("  CIPHER v1.0", "WAVE TO WAKE");
    while (ir_detected() == 0);
    buzzer_tone(1000, 200);
    HAL_Delay(300);
    oled_show_2lines("PRESENT CARD", "  TO UNLOCK");
    while (RC522_Detect() == 0);
    buzzer_tone(1200, 150);
    HAL_Delay(100);
    buzzer_tone(1500, 150);
    HAL_Delay(200);
    oled_show_2lines("HACK INITIATED", "    90s");
    HAL_Delay(1000);
    game_state = STATE_PLAYING;
    HAL_TIM_Base_Start_IT(&htim2);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void run_stage(void) {
  Stage* s = &stages[current_stage];
  char header[20];
  snprintf(header, sizeof(header), "STAGE %d/6", current_stage + 1);

  if (s->type == STAGE_POT) {
    while (game_state == STATE_PLAYING) {
      uint8_t val = read_pot();
      char val_buf[16];
      snprintf(val_buf, sizeof(val_buf), "VAL: %02d", val);
      SSD1306_Fill(SSD1306_COLOR_BLACK);
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts(header, &Font_7x10, 1);
      SSD1306_GotoXY(0, 14);
      SSD1306_Puts(s->question, &Font_7x10, 1);
      SSD1306_GotoXY(0, 30);
      SSD1306_Puts(val_buf, &Font_7x10, 1);
      oled_show_timer();
      SSD1306_UpdateScreen();
      if (btn_pressed()) {
        HAL_Delay(50);
        if (abs(val - s->answer) <= s->tolerance)
          handle_pass();
        else
          handle_fail();
        return;
      }
    }
  }

  else if (s->type == STAGE_ULTRASONIC) {
    while (game_state == STATE_PLAYING) {
      uint32_t dist = hcsr04_read_cm();
      char dist_buf[20];
      if (dist >= 999)
        snprintf(dist_buf, sizeof(dist_buf), "DIST: --cm");
      else
        snprintf(dist_buf, sizeof(dist_buf), "DIST: %lucm", dist);
      SSD1306_Fill(SSD1306_COLOR_BLACK);
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts(header, &Font_7x10, 1);
      SSD1306_GotoXY(0, 14);
      SSD1306_Puts(s->question, &Font_7x10, 1);
      SSD1306_GotoXY(0, 30);
      SSD1306_Puts(dist_buf, &Font_7x10, 1);
      oled_show_timer();
      SSD1306_UpdateScreen();
      if (btn_pressed()) {
        HAL_Delay(50);
        if (dist < 999 && abs((int)dist - s->answer) <= 3)
          handle_pass();
        else
          handle_fail();
        return;
      }
      HAL_Delay(100);
    }
  }

  else if (s->type == STAGE_BUTTON_HOLD) {
    while (btn_pressed());
    while (!btn_pressed() && game_state == STATE_PLAYING) {
      SSD1306_Fill(SSD1306_COLOR_BLACK);
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts(header, &Font_7x10, 1);
      SSD1306_GotoXY(0, 14);
      SSD1306_Puts("HOLD BTN FOR 4s", &Font_7x10, 1);
      SSD1306_GotoXY(0, 30);
      SSD1306_Puts("PRESS TO START", &Font_7x10, 1);
      oled_show_timer();
      SSD1306_UpdateScreen();
    }
    uint32_t press_start = HAL_GetTick();
    while (btn_pressed() && game_state == STATE_PLAYING) {
      uint32_t held = HAL_GetTick() - press_start;
      char h_buf[20];
      snprintf(h_buf, sizeof(h_buf), "HELD: %lu.%lus", held / 1000,
               (held % 1000) / 100);
      SSD1306_Fill(SSD1306_COLOR_BLACK);
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts(header, &Font_7x10, 1);
      SSD1306_GotoXY(0, 14);
      SSD1306_Puts("HOLD BTN FOR 4s", &Font_7x10, 1);
      SSD1306_GotoXY(0, 30);
      SSD1306_Puts(h_buf, &Font_7x10, 1);
      oled_show_timer();
      SSD1306_UpdateScreen();
      HAL_Delay(100);
    }
    uint32_t held_ms = HAL_GetTick() - press_start;
    if (abs((int)held_ms - s->hold_time_ms) <= 1500)
      handle_pass();
    else
      handle_fail();
  }

  else if (s->type == STAGE_DHT_KEYPAD) {
    uint8_t temp = 0, hum = 0;
    if (!dht11_read(&temp, &hum)) temp = 25;
    int answer = ((temp + 2) / 5) * 5;
    char q_buf[20];
    snprintf(q_buf, sizeof(q_buf), "TEMP:%dC RND/5", temp);
    while (game_state == STATE_PLAYING) {
      uint8_t val = read_pot();
      char val_buf[16];
      snprintf(val_buf, sizeof(val_buf), "ANS: %02d", val);
      SSD1306_Fill(SSD1306_COLOR_BLACK);
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts(header, &Font_7x10, 1);
      SSD1306_GotoXY(0, 14);
      SSD1306_Puts(q_buf, &Font_7x10, 1);
      SSD1306_GotoXY(0, 30);
      SSD1306_Puts(val_buf, &Font_7x10, 1);
      oled_show_timer();
      SSD1306_UpdateScreen();
      if (btn_pressed()) {
        HAL_Delay(50);
        if (abs(val - answer) <= 2)
          handle_pass();
        else
          handle_fail();
        return;
      }
    }
  }

  else if (s->type == STAGE_KEYPAD_PASSCODE) {
    int code1 = 0, code2 = 0;
    int conf1 = 0, conf2 = 0;

    // SET: first half (digits 1&2)
    while (game_state == STATE_PLAYING) {
      uint8_t val = read_pot();
      char set_buf[20];
      snprintf(set_buf, sizeof(set_buf), "DIG 1&2: %02d", val);
      SSD1306_Fill(SSD1306_COLOR_BLACK);
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts(header, &Font_7x10, 1);
      SSD1306_GotoXY(0, 14);
      SSD1306_Puts("SET PASSCODE:", &Font_7x10, 1);
      SSD1306_GotoXY(0, 30);
      SSD1306_Puts(set_buf, &Font_7x10, 1);
      oled_show_timer();
      SSD1306_UpdateScreen();
      if (btn_pressed()) {
        HAL_Delay(200);
        code1 = val;
        break;
      }
    }

    // SET: second half (digits 3&4)
    while (game_state == STATE_PLAYING) {
      uint8_t val = read_pot();
      char set_buf[20];
      snprintf(set_buf, sizeof(set_buf), "DIG 3&4: %02d", val);
      SSD1306_Fill(SSD1306_COLOR_BLACK);
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts(header, &Font_7x10, 1);
      SSD1306_GotoXY(0, 14);
      SSD1306_Puts("SET PASSCODE:", &Font_7x10, 1);
      SSD1306_GotoXY(0, 30);
      SSD1306_Puts(set_buf, &Font_7x10, 1);
      oled_show_timer();
      SSD1306_UpdateScreen();
      if (btn_pressed()) {
        HAL_Delay(200);
        code2 = val;
        break;
      }
    }

    // CONFIRM: first half
    while (game_state == STATE_PLAYING) {
      uint8_t val = read_pot();
      char con_buf[20];
      snprintf(con_buf, sizeof(con_buf), "DIG 1&2: %02d", val);
      SSD1306_Fill(SSD1306_COLOR_BLACK);
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts(header, &Font_7x10, 1);
      SSD1306_GotoXY(0, 14);
      SSD1306_Puts("CONFIRM CODE:", &Font_7x10, 1);
      SSD1306_GotoXY(0, 30);
      SSD1306_Puts(con_buf, &Font_7x10, 1);
      oled_show_timer();
      SSD1306_UpdateScreen();
      if (btn_pressed()) {
        HAL_Delay(200);
        conf1 = val;
        break;
      }
    }

    // CONFIRM: second half
    while (game_state == STATE_PLAYING) {
      uint8_t val = read_pot();
      char con_buf[20];
      snprintf(con_buf, sizeof(con_buf), "DIG 3&4: %02d", val);
      SSD1306_Fill(SSD1306_COLOR_BLACK);
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts(header, &Font_7x10, 1);
      SSD1306_GotoXY(0, 14);
      SSD1306_Puts("CONFIRM CODE:", &Font_7x10, 1);
      SSD1306_GotoXY(0, 30);
      SSD1306_Puts(con_buf, &Font_7x10, 1);
      oled_show_timer();
      SSD1306_UpdateScreen();
      if (btn_pressed()) {
        HAL_Delay(200);
        conf2 = val;
        break;
      }
    }

    if (abs(code1 - conf1) <= 2 && abs(code2 - conf2) <= 2)
      handle_pass();
    else
      handle_fail();
  }
}
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  uint32_t now = HAL_GetTick();
  if (now - key_tick < 200) return;
  key_tick = now;

  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);

  const uint16_t ROWS[4] = {GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_8, GPIO_PIN_2};
  const char MAP[4][4] = {{'1', '2', '3', 'A'},
                          {'4', '5', '6', 'B'},
                          {'7', '8', '9', 'C'},
                          {'*', '0', '#', 'D'}};
  const uint8_t col = (GPIO_Pin == GPIO_PIN_9)    ? 0
                      : (GPIO_Pin == GPIO_PIN_10) ? 1
                      : (GPIO_Pin == GPIO_PIN_11) ? 2
                                                  : 3;

  for (int r = 0; r < 4; r++) {
    HAL_GPIO_WritePin(GPIOB, ROWS[0] | ROWS[1] | ROWS[2] | ROWS[3],
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, ROWS[r], GPIO_PIN_RESET);
    delay_us(10);
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_Pin) == GPIO_PIN_SET) {
      last_key = MAP[r][col];
      break;
    }
  }

  HAL_GPIO_WritePin(GPIOB, ROWS[0] | ROWS[1] | ROWS[2] | ROWS[3], GPIO_PIN_SET);

  g.Mode = GPIO_MODE_IT_RISING;
  g.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &g);
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t* file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
