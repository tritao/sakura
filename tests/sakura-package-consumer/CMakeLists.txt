cmake_minimum_required(VERSION 3.15)
project(sakura-package-consumer C)

find_package(Sakura CONFIG REQUIRED)

add_executable(sakura-package-consumer main.c)
target_link_libraries(sakura-package-consumer PRIVATE Sakura::ControlClient)
