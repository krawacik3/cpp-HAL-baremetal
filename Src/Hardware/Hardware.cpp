#include <Hardware.hpp>
#include <event_groups.h>

std::array<Uart::State, 2> Hardware::uartStates;

void Hardware::enableGpio(GPIO_TypeDef* gpio, uint32_t pin, Gpio::Mode direction, Gpio::Pull pull) {

    GPIO_InitTypeDef initTypeDef;
    initTypeDef.Pin = pin;
    switch (direction) {
        case Gpio::Mode::Input:
            initTypeDef.Mode = GPIO_MODE_INPUT;
            break;
        case Gpio::Mode::Output:
            initTypeDef.Mode = GPIO_MODE_OUTPUT_PP;
            break;
        case Gpio::Mode::AlternateInput:
            initTypeDef.Mode = GPIO_MODE_AF_INPUT;
            break;
        case Gpio::Mode::AlternatePP:
            initTypeDef.Mode = GPIO_MODE_AF_PP;
    }
    initTypeDef.Pull = static_cast<uint32_t>(pull);
    initTypeDef.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(gpio, &initTypeDef);
}

void Hardware::toggle(GPIO_TypeDef* gpio, uint32_t pin) {
    HAL_GPIO_TogglePin(gpio, pin);
}

void Hardware::configureClocks() {
    RCC_ClkInitTypeDef clkInitStruct;
    clkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK;
    clkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    HAL_RCC_ClockConfig(&clkInitStruct, FLASH_LATENCY_0);
    // Systick is executed once every 1ms
    SysTick_Config(SystemCoreClock / 1000);

    // Enable clocks for GPIOs
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
}

void Hardware::initializeUart(Uart::Uart id, uint32_t baudRate) {
    switch (id) {
        case Uart::Uart::UART_1:
            __HAL_RCC_GPIOA_CLK_ENABLE();
            __HAL_RCC_USART1_CLK_ENABLE();
            enableGpio(GPIOA, GPIO_PIN_9, Gpio::Mode::AlternatePP, Gpio::Pull::NoPull);  // TX1
            enableGpio(GPIOA, GPIO_PIN_10, Gpio::Mode::AlternateInput, Gpio::Pull::Pullup);  // RX1

            HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
            HAL_NVIC_EnableIRQ(USART1_IRQn);
            break;
        case Uart::Uart::UART_2:
            __HAL_RCC_GPIOA_CLK_ENABLE();
            __HAL_RCC_USART2_CLK_ENABLE();
            enableGpio(GPIOA, GPIO_PIN_2, Gpio::Mode::AlternatePP, Gpio::Pull::NoPull);  // TX2
            enableGpio(GPIOA, GPIO_PIN_3, Gpio::Mode::AlternateInput, Gpio::Pull::Pullup);  // RX2
            HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
            HAL_NVIC_EnableIRQ(USART2_IRQn);
            break;
    }

    Uart::State& state = getUartState(id);
    state.handle.Instance = id == Uart::Uart::UART_1 ? USART1 : USART2;
    state.handle.Init.BaudRate = baudRate;
    state.handle.Init.WordLength = UART_WORDLENGTH_8B;
    state.handle.Init.Parity = UART_PARITY_NONE;
    state.handle.Init.StopBits = UART_STOPBITS_1;
    state.handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    state.handle.Init.OverSampling = UART_OVERSAMPLING_16;
    state.handle.Init.Mode = UART_MODE_TX_RX;

    HAL_UART_Init(&state.handle);

    __HAL_UART_ENABLE_IT(&state.handle, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&state.handle, UART_IT_TC);

    // Clear bits
    state.txRxState = xEventGroupCreate();
    xEventGroupClearBits(state.txRxState, Uart::State::rxBit | Uart::State::txBit);
}

void Hardware::uartSend(Uart::Uart id, uint8_t *data, size_t numOfBytes) {
    Uart::State& state = getUartState(id);
    if(state.txRxState) {
        // Check if there is no transmission
        if((xEventGroupGetBits(state.txRxState) & Uart::State::txBit) == 0) {
            HAL_UART_Transmit_IT(&state.handle, data, numOfBytes);
            xEventGroupSetBits(state.txRxState, Uart::State::txBit);
        }
    }
}

void Hardware::uartReceive(Uart::Uart id, uint8_t *data, size_t numOfBytes) {
    Uart::State& state = getUartState(id);
    if(state.txRxState) {
        // Check if there is no transmission
        if((xEventGroupGetBits(state.txRxState) & Uart::State::rxBit) == 0) {
            HAL_UART_Receive_IT(&state.handle, data, numOfBytes);
            xEventGroupSetBits(state.txRxState, Uart::State::rxBit);
        }
    }
}

bool Hardware::isUartTxComplete(Uart::Uart id) {
    const Uart::State& state = getUartState(id);
    return (xEventGroupGetBits(state.txRxState) & Uart::State::txBit) == 0;
}

bool Hardware::isUartRxComplete(Uart::Uart id) {
    const Uart::State& state = getUartState(id);
    return (xEventGroupGetBits(state.txRxState) & Uart::State::rxBit) == 0;
}

void Hardware::abortUartTx(Uart::Uart id) {
    HAL_UART_AbortTransmit_IT(&getUartState(id).handle);
    xEventGroupClearBits(getUartState(id).txRxState, Uart::State::txBit);
}

void Hardware::abortUartRx(Uart::Uart id) {
    HAL_UART_AbortReceive_IT(&getUartState(id).handle);
    xEventGroupClearBits(getUartState(id).txRxState, Uart::State::rxBit);
}

Uart::State& Hardware::getUartState(Uart::Uart id) {
    switch (id) {
        case Uart::Uart::UART_1:
            return uartStates[0];
        case Uart::Uart::UART_2:
            return uartStates[1];
    }
    // Give up and stay here
    return getUartState(id);
}

// Handlers for default HAL UART callbacks

void USART1_IRQHandler() {
    HAL_UART_IRQHandler(&Hardware::getUartState(Uart::Uart::UART_1).handle);
}

void USART2_IRQHandler() {
    HAL_UART_IRQHandler(&Hardware::getUartState(Uart::Uart::UART_2).handle);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if(huart->Instance == USART1){
        if(auto* eventGroup = Hardware::getUartState(Uart::Uart::UART_1).txRxState) {
            xEventGroupClearBitsFromISR(eventGroup, Uart::State::txBit);
        }
    }
    else if(huart->Instance == USART2){
        if(auto* eventGroup = Hardware::getUartState(Uart::Uart::UART_2).txRxState) {
            xEventGroupClearBitsFromISR(eventGroup, Uart::State::txBit);
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if(huart->Instance == USART1){
        if(auto* eventGroup = Hardware::getUartState(Uart::Uart::UART_1).txRxState) {
            xEventGroupClearBitsFromISR(eventGroup, Uart::State::rxBit);
        }
    }
    if(huart->Instance == USART2){
        if(auto* eventGroup = Hardware::getUartState(Uart::Uart::UART_1).txRxState) {
            xEventGroupClearBitsFromISR(eventGroup, Uart::State::rxBit);
        }
    }
}
