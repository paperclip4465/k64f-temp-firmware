cmake_minimum_required(VERSION 3.20.0)

set(BOARD "frdm_k64f")

find_package(Zephyr REQUIRED HINTS $ENV(ZEPHYR_BASE))

project(k64_temp)

target_sources(app PRIVATE
  src/main.c)
