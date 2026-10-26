################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/Wifi/Src/es_wifi.c \
../Drivers/Wifi/Src/es_wifi_io.c \
../Drivers/Wifi/Src/wifi.c 

OBJS += \
./Drivers/Wifi/Src/es_wifi.o \
./Drivers/Wifi/Src/es_wifi_io.o \
./Drivers/Wifi/Src/wifi.o 

C_DEPS += \
./Drivers/Wifi/Src/es_wifi.d \
./Drivers/Wifi/Src/es_wifi_io.d \
./Drivers/Wifi/Src/wifi.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/Wifi/Src/%.o Drivers/Wifi/Src/%.su Drivers/Wifi/Src/%.cyclo: ../Drivers/Wifi/Src/%.c Drivers/Wifi/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L475xx -c -I"C:/Users/joris/STM32CubeIDE/workspace_2.2.0/FW_nominal/Drivers/BSP/Components/hts221" -I../Core/Inc -I"C:/Users/joris/STM32CubeIDE/workspace_2.2.0/FW_nominal/Drivers/Wifi/Inc" -I"C:/Users/joris/STM32CubeIDE/workspace_2.2.0/FW_nominal/Drivers/BSP/B-L475E-IOT01" -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-Wifi-2f-Src

clean-Drivers-2f-Wifi-2f-Src:
	-$(RM) ./Drivers/Wifi/Src/es_wifi.cyclo ./Drivers/Wifi/Src/es_wifi.d ./Drivers/Wifi/Src/es_wifi.o ./Drivers/Wifi/Src/es_wifi.su ./Drivers/Wifi/Src/es_wifi_io.cyclo ./Drivers/Wifi/Src/es_wifi_io.d ./Drivers/Wifi/Src/es_wifi_io.o ./Drivers/Wifi/Src/es_wifi_io.su ./Drivers/Wifi/Src/wifi.cyclo ./Drivers/Wifi/Src/wifi.d ./Drivers/Wifi/Src/wifi.o ./Drivers/Wifi/Src/wifi.su

.PHONY: clean-Drivers-2f-Wifi-2f-Src

