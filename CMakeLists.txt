cmake_minimum_required(VERSION 3.20)
project(stm32flash C)

add_compile_options(-Wall -g)

set(CMAKE_C_STANDARD 11)

add_executable(stm32flash
        main.c
        dev_table.c
        init.c
        port.c
        serial_platform.c
        utils.c
        i2c.c
        serial_common.c
        stm32.c
        binary.c
        hex.c
        network_posix.c
        )
