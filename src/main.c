
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"

#include "stm32f4xx.h"
#include "defines.h"
#include "tm_stm32f4_delay.h"
#include "tm_stm32f4_fatfs.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4_discovery.h"
#include <stdio.h>
#include <string.h>

/* Private typedef -----------------------------------------------------------*/
GPIO_InitTypeDef  GPIO_InitStructure;

/* Private define ------------------------------------------------------------*/
#define FROM_MILLIS(x) (x / portTICK_RATE_MS)
/* Audio file size and start address are defined here since the audio file is 
stored in Flash memory as a constant table of 16-bit data */
#define AUDIO_FILE_SZE          990000
#define AUIDO_START_ADDRESS     58 /* Offset relative to audio file header size */

/* Private variables ---------------------------------------------------------*/
RCC_ClocksTypeDef RCC_Clocks;
__IO uint8_t RepeatState = 0;
__IO uint16_t CCR_Val = 16826;
xQueueHandle ledQ, musicQ;

extern __IO uint8_t AudioPlayStart;
extern uint16_t AUDIO_SAMPLE[];
extern __IO uint8_t PauseResumeStatus ;

/* Private function prototypes -----------------------------------------------*/
static void PlayMusic(void *pvParameters);
static void ReadSDCard(void *pvParameters);
static void ControlLED3(void *pvParameters);
static void DetectButtonPress(void *pvParameters);

void initHW();

/* Private functions ---------------------------------------------------------*/

/**
* @brief  Main program.
* @param  None
* @retval None
*/
int main(void)
{   
  initHW();
  
  /* Create IPC variables */
  ledQ = xQueueCreate(10, sizeof(int));
  musicQ = xQueueCreate(10, sizeof(int));  
  if (ledQ == 0 || musicQ == 0) {
    while(1); /* fatal error */
  }
  
  xTaskCreate(PlayMusic, (signed char*)"PlayMusic", 1024, NULL, 1, NULL);
  xTaskCreate(ControlLED3, (signed char*)"ControlLED3", 128, NULL, 4, NULL);    
  xTaskCreate(DetectButtonPress, (signed char*)"DetectButtonPress", 128, NULL, 1, NULL);
  xTaskCreate(ReadSDCard, (signed char*)"ReadSDCard", 4096, NULL, 2, NULL);
  vTaskStartScheduler();  
  
  while (1);
}

void initHW()
{  
  /* Initialize LEDS */
  STM_EVAL_LEDInit(LED3);
  STM_EVAL_LEDInit(LED4);
  STM_EVAL_LEDInit(LED5);
  STM_EVAL_LEDInit(LED6);  
  
  /* SysTick end of count event each 10ms */
  RCC_GetClocksFreq(&RCC_Clocks);
  SysTick_Config(RCC_Clocks.HCLK_Frequency / 100);
  
  // Init PushButton
  STM_EVAL_PBInit(BUTTON_USER, BUTTON_MODE_GPIO);
}

static void DetectButtonPress(void *pvParameters) {
  int sig = 1, ss = 1;
  while (1) {
    /* Detect Button Press  */
    if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0) > 0) {
      while(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0) > 0)
        vTaskDelay(100 / portTICK_RATE_MS);
      
      if (ss == 1) {
        xQueueSendToBack(ledQ, &sig, 0); 
      }
      if (ss == 0) {
        xQueueSendToBack(musicQ, &sig, 0); 
      }
      ss = !ss;
    }
  }
}

static void ReadSDCard(void *pvParameters) {
  FATFS FatFs;
  FIL fil;
  
  // Turn on RED led then wait for SD card to be ready
  STM_EVAL_LEDOn(LED5);
  printf("Waiting for SD card...\n");
  while (f_mount(&FatFs, "0:", 1) != FR_OK) {
    vTaskDelay(FROM_MILLIS(1000));
  }
  
  // Open file and keep toggle RED led if error occurs
  printf("Opening file...\n");
  while (f_open(&fil, "0:1stfile.txt", FA_OPEN_ALWAYS | FA_READ | FA_WRITE) != FR_OK) {
    vTaskDelay(FROM_MILLIS(500));     
    STM_EVAL_LEDToggle(LED5);
  }
  
  STM_EVAL_LEDOff(LED5);
  STM_EVAL_LEDOn(LED4);
  printf("Reading...\n");
  TCHAR line[100];
  while (1) {
    TCHAR* result = f_gets(line, 100, &fil);
    if (result != NULL) {    
      printf("%s", line);
      STM_EVAL_LEDToggle(LED4);
    }
    else { 
      printf("EOF");
      break;
    }
    vTaskDelay(FROM_MILLIS(1000));
  }   
  f_close(&fil);
  f_mount(0, "0:", 1);
  
  STM_EVAL_LEDOff(LED4);
  while(1) {        
    STM_EVAL_LEDToggle(LED5);
    vTaskDelay(FROM_MILLIS(300));
  }
}

static void ControlLED3(void *pvParameters)
{ 
  int sig = 1, state = 1;
  portBASE_TYPE status;  
  
  for(;;) 
  {    
    status = xQueueReceive(ledQ, &sig, 0);
    if (status == pdTRUE) {
      state = !state;
    }
    
    if (state) {
      STM_EVAL_LEDToggle(LED3);
    }
    else {
      STM_EVAL_LEDOn(LED3);
    }
    vTaskDelay(FROM_MILLIS(500));
  }
}

static void PlayMusic(void *pvParameters)
{    
  /* Start playing */
  AudioPlayStart = 1;
  RepeatState =0;
  int sig = 1, isPausing = 0;
  portBASE_TYPE status;  
  
  /* Initialize wave player (Codec, DMA, I2C) */
  WavePlayerInit(I2S_AudioFreq_48k);
  
  /* Play on */
  AudioFlashPlay((uint16_t*)(AUDIO_SAMPLE + AUIDO_START_ADDRESS),AUDIO_FILE_SZE,AUIDO_START_ADDRESS);
  
  /* Infinite loop */
  while(1)
  { 
    /* check on the repeate status */
    if (RepeatState == 0)
    {      
      status = xQueueReceive(musicQ, &sig, 0);
      if (status == pdTRUE) {
        PauseResumeStatus = isPausing ? 1 : 0;
        isPausing = !isPausing;
      }
      
      if (PauseResumeStatus == 0)
      {
        /* Pause playing */
        WavePlayerPauseResume(PauseResumeStatus);
        PauseResumeStatus = 2;
        STM_EVAL_LEDOn(LED6);
      }
      else if (PauseResumeStatus == 1)
      {
        /* Resume playing */
        WavePlayerPauseResume(PauseResumeStatus);
        PauseResumeStatus = 2;
      }
      else
      {        
        STM_EVAL_LEDToggle(LED6);
      }
    }
    else
    {
      /* Stop playing */
      WavePlayerStop();
    }
    
    vTaskDelay(FROM_MILLIS(200));
  }
}

#ifdef  USE_FULL_ASSERT

/**
* @brief  Reports the name of the source file and the source line number
*   where the assert_param error has occurred.
* @param  file: pointer to the source file name
* @param  line: assert_param error line source number
* @retval None
*/
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* User can add his own implementation to report the file name and line number,
  ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  
  /* Infinite loop */
  while (1)
  {
  }
}
#endif


/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
