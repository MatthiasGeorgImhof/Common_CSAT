#ifndef MOCK_HAL_UART_H
#define MOCK_HAL_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <cstddef>
#include <stdint.h>
#include <stdbool.h>

// Include core definitions
#include "mock_hal/mock_hal_core.h"

#define MOCK_HAL_UART_ENABLED

//--- UART_State_Definition UART State Code Definition
#define  HAL_UART_STATE_RESET         0x00000000U
#define  HAL_UART_STATE_READY         0x00000020U
#define  HAL_UART_STATE_BUSY          0x00000024U
#define  HAL_UART_STATE_BUSY_TX       0x00000021U
#define  HAL_UART_STATE_BUSY_RX       0x00000022U
#define  HAL_UART_STATE_BUSY_TX_RX    0x00000023U
#define  HAL_UART_STATE_TIMEOUT       0x000000A0U
#define  HAL_UART_STATE_ERROR         0x000000E0U

//--- UARTEx Defines ---
#define HAL_UART_RXEVENT_HT   0  // UART Rx Event: Half Transfer
#define HAL_UART_RXEVENT_IDLE 1  // UART Rx Event: Idle Line

//--- UART Structures ---
typedef struct {
    uint32_t BaudRate;       // Specifies the baud rate
    uint32_t WordLength;     // Specifies the word length
    uint32_t StopBits;       // Specifies the stop bits
    uint32_t Parity;         // Specifies the parity
    uint32_t Mode;           // Specifies the mode (Tx, Rx, or TxRx)
    uint32_t HwFlowCtl;      // Specifies the hardware flow control
    uint32_t OverSampling;    // Specifies the oversampling mode
    uint32_t OneBitSampling;  // Specifies one-bit sampling
    uint32_t ADVFEATURE;      // Specifies advanced features
} UART_InitTypeDef;

typedef struct {
   UART_InitTypeDef Init;   // UART initialization structure
} UART_HandleTypeDef;

typedef uint32_t HAL_UART_StateTypeDef;
typedef uint32_t HAL_UART_RxEventTypeTypeDef; // Type for UART Rx Event

//--- UART Mock Function Prototypes ---
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);

HAL_UART_StateTypeDef HAL_UART_GetState(UART_HandleTypeDef *huart);

//--- UART Helper Function Prototypes ---
void inject_uart_rx_data(uint8_t *data, size_t size);
void clear_uart_rx_buffer();
void clear_uart_tx_buffer();

//--- Getter Function Prototypes ---
size_t get_uart_tx_buffer_count();
uint8_t* get_uart_tx_buffer();
void init_uart_handle(UART_HandleTypeDef *huart);

//--- UARTEx Mock Function Prototypes ---
HAL_StatusTypeDef HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);

//--- UARTEx External Variables ---
extern uint32_t mocked_uart_rx_event_type;
void set_mocked_uart_rx_event_type(uint32_t event_type);

void set_mocked_uart_state(HAL_UART_StateTypeDef state);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_HAL_UART_H */
